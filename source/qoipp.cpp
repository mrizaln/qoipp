#include "qoipp.hpp"

#include <array>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <format>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef QOIPP_DEBUG
#    include <unordered_map>
#    include <iostream>
#endif

#if defined(_MSC_VER)
#    define QOIPP_ALWAYS_INLINE [[msvc::forceinline]]
#elif defined(__GNUC__)
#    define QOIPP_ALWAYS_INLINE [[gnu::always_inline]]
#elif defined(__clang__)
#    define QOIPP_ALWAYS_INLINE [[clang::always_inline]]
#else
#    define QOIPP_ALWAYS_INLINE
#endif

namespace sv = std::views;

// utils ana aliases
namespace qoipp
{
    using i8  = std::int8_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;

    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;

    using usize = std::size_t;
    using isize = std::ptrdiff_t;

    using f32 = float;
    using f64 = double;

    using Byte = std::byte;

    template <usize N>
    using ByteArr = std::array<Byte, N>;
    using ByteVec = std::vector<Byte>;

    template <typename T, typename... Ts>
    concept AnyOf = (std::same_as<T, Ts> or ...);

    template <typename T>
    concept ByteRange = std::ranges::range<T> and std::same_as<std::ranges::range_value_t<T>, Byte>;

    template <typename T, usize N>
    consteval ByteArr<N> toBytes(const T (&value)[N])
    {
        ByteArr<N> result;
        for (usize i : sv::iota(0u, N)) {
            result[i] = static_cast<Byte>(value[i]);
        }
        return result;
    }

    template <typename T>
        requires std::is_fundamental_v<T>
    constexpr T toBigEndian(const T& value) noexcept
    {
        if constexpr (std::endian::native == std::endian::big) {
            return value;
        } else {
            T result  = 0;
            result   |= (value & 0x000000FF) << 24;
            result   |= (value & 0x0000FF00) << 8;
            result   |= (value & 0x00FF0000) >> 8;
            result   |= (value & 0xFF000000) >> 24;
            return result;
        }
    }

    template <typename T>
        requires std::is_fundamental_v<T>
    constexpr T fromBigEndian(const T& value) noexcept
    {
        return toBigEndian(value);
    }

    enum class Channels
    {
        RGB  = 3,
        RGBA = 4,
    };

    struct Pixel
    {
        u8 m_r;
        u8 m_g;
        u8 m_b;
        u8 m_a = 0xFF;

        constexpr auto operator<=>(const Pixel&) const = default;
    };
}

namespace qoipp::constants
{
    constexpr usize      headerSize       = 14;
    constexpr usize      runningArraySize = 64;
    constexpr ByteArr<8> endMarker        = toBytes({ 0, 0, 0, 0, 0, 0, 0, 1 });

    constexpr i8 biasOpRun    = -1;
    constexpr i8 biasOpDiff   = 2;
    constexpr i8 biasOpLumaG  = 32;
    constexpr i8 biasOpLumaRB = 8;
    constexpr i8 runLimit     = 62;
    constexpr i8 minDiff      = -2;
    constexpr i8 maxDiff      = 1;
    constexpr i8 minLumaG     = -32;
    constexpr i8 maxLumaG     = 31;
    constexpr i8 minLumaRB    = -8;
    constexpr i8 maxLumaRB    = 7;

    constexpr Pixel start = { 0x00, 0x00, 0x00, 0xFF };
}

namespace qoipp::data
{
    // clang-format off
    template <typename T, typename R>
    concept DataChunk = std::ranges::range<R> and requires(const T t, R& r, usize& i) {
        { t.write(r, i) } noexcept -> std::same_as<void>;
    };
    // clang-format on

    template <typename T>
    concept DataChunkVec = DataChunk<T, ByteVec>;

    QOIPP_ALWAYS_INLINE inline void write32(ByteVec& vec, usize& index, u32 value) noexcept
    {
        vec[index++] = static_cast<Byte>((value >> 24) & 0xFF);
        vec[index++] = static_cast<Byte>((value >> 16) & 0xFF);
        vec[index++] = static_cast<Byte>((value >> 8) & 0xFF);
        vec[index++] = static_cast<Byte>(value & 0xFF);
    }

    struct QoiHeader
    {
        std::array<char, 4> m_magic = { 'q', 'o', 'i', 'f' };

        u32 m_width;
        u32 m_height;
        u8  m_channels;
        u8  m_colorspace;

        QOIPP_ALWAYS_INLINE void write(ByteVec& vec, usize& index) const noexcept
        {
            for (char c : m_magic) {
                vec[index++] = static_cast<Byte>(c);
            }

            write32(vec, index, m_width);
            write32(vec, index, m_height);

            vec[index++] = static_cast<Byte>(m_channels);
            vec[index++] = static_cast<Byte>(m_colorspace);
        }
    };
    static_assert(DataChunkVec<QoiHeader>);

    struct EndMarker
    {
        QOIPP_ALWAYS_INLINE static void write(ByteVec& vec, usize& index) noexcept
        {
            for (auto byte : constants::endMarker) {
                vec[index++] = byte;
            }
        }
    };
    static_assert(DataChunkVec<EndMarker>);

    namespace op
    {
        enum Tag : u8
        {
            OP_RGB   = 0b11111110,
            OP_RGBA  = 0b11111111,
            OP_INDEX = 0b00000000,
            OP_DIFF  = 0b01000000,
            OP_LUMA  = 0b10000000,
            OP_RUN   = 0b11000000,
        };

        struct Rgb
        {
            u8 m_r = 0;
            u8 m_g = 0;
            u8 m_b = 0;

            QOIPP_ALWAYS_INLINE void write(ByteVec& vec, usize& index) const noexcept
            {
                vec[index++] = static_cast<Byte>(OP_RGB);
                vec[index++] = static_cast<Byte>(m_r);
                vec[index++] = static_cast<Byte>(m_g);
                vec[index++] = static_cast<Byte>(m_b);
            }
        };
        static_assert(DataChunkVec<Rgb>);

        struct Rgba
        {
            u8 m_r = 0;
            u8 m_g = 0;
            u8 m_b = 0;
            u8 m_a = 0;

            QOIPP_ALWAYS_INLINE void write(ByteVec& vec, usize& index) const noexcept
            {
                vec[index++] = static_cast<Byte>(OP_RGBA);
                vec[index++] = static_cast<Byte>(m_r);
                vec[index++] = static_cast<Byte>(m_g);
                vec[index++] = static_cast<Byte>(m_b);
                vec[index++] = static_cast<Byte>(m_a);
            }
        };
        static_assert(DataChunkVec<Rgba>);

        struct Index
        {
            u32 m_index = 0;

            QOIPP_ALWAYS_INLINE void write(ByteVec& vec, usize& index) const noexcept
            {
                vec[index++] = static_cast<Byte>(OP_INDEX | m_index);
            }
        };
        static_assert(DataChunkVec<Index>);

        struct Diff
        {
            i8 m_dr = 0;
            i8 m_dg = 0;
            i8 m_db = 0;

            QOIPP_ALWAYS_INLINE void write(ByteVec& vec, usize& index) const noexcept
            {
                constexpr auto bias = constants::biasOpDiff;

                vec[index++] = static_cast<Byte>(
                    OP_DIFF | (m_dr + bias) << 4 | (m_dg + bias) << 2 | (m_db + bias)
                );
            }
        };
        static_assert(DataChunkVec<Diff>);

        struct Luma
        {
            i8 m_dg    = 0;
            i8 m_dr_dg = 0;
            i8 m_db_dg = 0;

            QOIPP_ALWAYS_INLINE void write(ByteVec& vec, usize& index) const noexcept
            {
                constexpr auto biasG  = constants::biasOpLumaG;
                constexpr auto biasRB = constants::biasOpLumaRB;

                vec[index++] = static_cast<Byte>(OP_LUMA | (m_dg + biasG));
                vec[index++] = static_cast<Byte>((m_dr_dg + biasRB) << 4 | (m_db_dg + biasRB));
            }
        };
        static_assert(DataChunkVec<Luma>);

        struct Run
        {
            i8 m_run = 0;

            QOIPP_ALWAYS_INLINE void write(ByteVec& vec, usize& index) const noexcept
            {
                vec[index++] = static_cast<Byte>(OP_RUN | (m_run + constants::biasOpRun));
            }
        };
        static_assert(DataChunkVec<Run>);

        template <typename T>
        concept Op = AnyOf<T, Rgb, Rgba, Index, Diff, Luma, Run>;

        QOIPP_ALWAYS_INLINE inline bool shouldDiff(i8 dr, i8 dg, i8 db) noexcept
        {
            return dr >= constants::minDiff && dr <= constants::maxDiff    //
                && dg >= constants::minDiff && dg <= constants::maxDiff    //
                && db >= constants::minDiff && db <= constants::maxDiff;
        }

        QOIPP_ALWAYS_INLINE inline bool shouldLuma(i8 dg, i8 dr_dg, i8 db_dg) noexcept
        {
            return dr_dg >= constants::minLumaRB && dr_dg <= constants::maxLumaRB    //
                && db_dg >= constants::minLumaRB && db_dg <= constants::maxLumaRB    //
                && dg >= constants::minLumaG && dg <= constants::maxLumaG;
        }
    }
}

namespace qoipp::impl
{
    class DataChunkArray
    {
    public:
        DataChunkArray(usize size)
            : m_bytes(size)
        {
        }

#ifdef QOIPP_DEBUG
        ~DataChunkArray()
        {
            std::cerr << "Op stats:\n";
            for (auto& [k, c] : m_opCount) {
                std::cerr << std::format("\t{}\t:{}\n", k, c);
            }
        }
#endif
        template <typename T>
            requires(data::op::Op<T> or AnyOf<T, data::QoiHeader, data::EndMarker>)
        QOIPP_ALWAYS_INLINE void push(T&& t) noexcept
        {
#ifdef QOIPP_DEBUG
            m_opCount[typeid(T).name()]++;
#endif
            t.write(m_bytes, m_index);
        }

        ByteVec get() noexcept
        {
            m_bytes.resize(m_index);
            return std::exchange(m_bytes, {});
        }

    private:
        ByteVec m_bytes;
        usize   m_index = 0;

#ifdef QOIPP_DEBUG
        std::unordered_map<std::string, i32> m_opCount;
#endif
    };

    using RunningArray = std::array<Pixel, constants::runningArraySize>;

    template <Channels Chan>
    QOIPP_ALWAYS_INLINE inline void getPixel(std::span<const Byte> data, Pixel& pixel, usize index) noexcept
    {
        const usize dataIndex = index * static_cast<u32>(Chan);

        if constexpr (Chan == Channels::RGB) {
            std::memcpy(&pixel, data.data() + dataIndex, 3);
            pixel.m_a = 0xFF;
        } else {
            std::memcpy(&pixel, data.data() + dataIndex, 4);
        }
    }

    QOIPP_ALWAYS_INLINE inline usize hash(const Pixel& pixel)
    {
        const auto& [r, g, b, a] = pixel;
        return (r * 3 + g * 5 + b * 7 + a * 11);
    }

    template <Channels Chan>
    ByteVec encode(std::span<const Byte> data, u32 width, u32 height, bool srgb)
    {

        const usize maxSize = width * height * static_cast<usize>(Chan)    //
                            + constants::headerSize                        //
                            + constants::endMarker.size();

        DataChunkArray chunks{ maxSize };    // the encoded data
        RunningArray   seenPixels = {};

        // TODO: correct colorspace field
        chunks.push(data::QoiHeader{
            .m_width      = static_cast<u32>(width),
            .m_height     = static_cast<u32>(height),
            .m_channels   = static_cast<u8>(Chan),
            .m_colorspace = static_cast<u8>(srgb ? 0 : 1),
        });

        auto prevPixel = constants::start;
        auto currPixel = constants::start;
        i32  run       = 0;

        for (const auto pixelIndex : sv::iota(0u, static_cast<usize>(width * height))) {
            getPixel<Chan>(data, currPixel, pixelIndex);

            if (prevPixel == currPixel) {
                run++;

                const bool runLimit  = run == constants::runLimit;
                const bool lastPixel = pixelIndex == static_cast<usize>(width * height) - 1;
                if (runLimit || lastPixel) {
                    chunks.push(data::op::Run{ .m_run = static_cast<i8>(run) });
                    run = 0;
                }
            } else {
                if (run > 0) {
                    // ends of OP_RUN
                    chunks.push(data::op::Run{ .m_run = static_cast<i8>(run) });
                    run = 0;
                }

                const u8 index = hash(currPixel) % constants::runningArraySize;

                // OP_INDEX
                if (seenPixels[index] == currPixel) {
                    chunks.push(data::op::Index{ .m_index = index });
                } else {
                    seenPixels[index] = currPixel;

                    // OP_DIFF and OP_LUMA
                    if (prevPixel.m_a == currPixel.m_a) {
                        const i8 dr = currPixel.m_r - prevPixel.m_r;
                        const i8 dg = currPixel.m_g - prevPixel.m_g;
                        const i8 db = currPixel.m_b - prevPixel.m_b;

                        const i8 dr_dg = dr - dg;
                        const i8 db_dg = db - dg;

                        if (data::op::shouldDiff(dr, dg, db)) {
                            chunks.push(data::op::Diff{
                                .m_dr = dr,
                                .m_dg = dg,
                                .m_db = db,
                            });
                        } else if (data::op::shouldLuma(dg, dr_dg, db_dg)) {
                            chunks.push(data::op::Luma{
                                .m_dg    = dg,
                                .m_dr_dg = dr_dg,
                                .m_db_dg = db_dg,
                            });
                        } else {
                            // OP_RGB
                            chunks.push(data::op::Rgb{
                                .m_r = currPixel.m_r,
                                .m_g = currPixel.m_g,
                                .m_b = currPixel.m_b,
                            });
                        }
                    } else if constexpr (Chan == Channels::RGBA) {
                        // OP_RGBA
                        chunks.push(data::op::Rgba{
                            .m_r = currPixel.m_r,
                            .m_g = currPixel.m_g,
                            .m_b = currPixel.m_b,
                            .m_a = currPixel.m_a,
                        });
                    }
                }
            }

            prevPixel = currPixel;
        }

        chunks.push(data::EndMarker{});

        return chunks.get();
    }
    // TODO: implement
    template <Channels Chan>
    QoiImage decode(std::span<const Byte> data) noexcept(false)
    {
        throw std::runtime_error{ "Not implemented yet" };
    }
}

namespace qoipp
{
    std::vector<Byte> encode(std::span<const Byte> data, ImageDesc desc) noexcept(false)
    {
        const auto [width, height, channels, colorspace] = desc;
        const auto maxSize                               = static_cast<usize>(width * height * channels);

        if (width <= 0 || height <= 0 || channels <= 0) {
            throw std::invalid_argument{
                std::format("Invalid image description: w = {}, h = {}, c = {}", width, height, channels)
            };
        }

        if (channels != 3 && channels != 4) {
            throw std::invalid_argument{
                std::format("Invalid number of channels: expected 3 or 4, got {}", channels)
            };
        }

        if (data.size() != maxSize) {
            throw std::invalid_argument{ std::format(
                "Data size does not match the image description: expected {} x {} x {} = {}, got {}",
                width,
                height,
                channels,
                maxSize,
                data.size()
            ) };
        }

        if (channels == 3 && data.size() % 3 != 0) {
            throw std::invalid_argument{
                "Data does not align with the number of channels: expected multiple of 3 bytes"
            };
        } else if (channels == 4 && data.size() % 4 != 0) {
            throw std::invalid_argument{
                "Data does not align with the number of channels: expected multiple of 4 bytes"
            };
        }

        bool isSrgb = colorspace == ColorSpace::sRGB;
        if (channels == 3) {
            return impl::encode<Channels::RGB>(data, (u32)width, (u32)height, isSrgb);
        } else {
            return impl::encode<Channels::RGBA>(data, (u32)width, (u32)height, isSrgb);
        }
    }

    // TODO: implement
    QoiImage decode(ByteSpan data) noexcept(false)
    {
        throw std::runtime_error{ "Not implemented yet" };
    }
}
