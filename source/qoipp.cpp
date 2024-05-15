#include "qoipp.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
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

namespace sr = std::ranges;
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
    constexpr T toBigEndian(const T& value)
    {
        if constexpr (std::endian::native == std::endian::big) {
            return value;
        } else {
            auto bytes = std::bit_cast<ByteArr<sizeof(T)>>(value);
            sr::reverse(bytes);
            return std::bit_cast<T>(bytes);
        }
    }

    template <typename T>
        requires std::is_fundamental_v<T>
    constexpr T fromBigEndian(const T& value)
    {
        return toBigEndian(value);
    }

    enum class Channels
    {
        RGB  = 3,
        RGBA = 4,
    };

    template <Channels>
    struct Pixel
    {
    };

    template <>
    struct Pixel<Channels::RGBA>
    {
        u8 m_r;
        u8 m_g;
        u8 m_b;
        u8 m_a = 0xFF;

        constexpr auto operator<=>(const Pixel&) const = default;
    };

    template <>
    struct Pixel<Channels::RGB>
    {
        u8 m_r;
        u8 m_g;
        u8 m_b;

        constexpr auto operator<=>(const Pixel&) const = default;
    };
}

namespace qoipp::constants
{
    template <Channels>
    struct StartPixel;

    template <>
    struct StartPixel<Channels::RGB>
    {
        constexpr static Pixel<Channels::RGB> value = { 0x00, 0x00, 0x00 };
    };

    template <>
    struct StartPixel<Channels::RGBA>
    {
        constexpr static Pixel<Channels::RGBA> value = { 0x00, 0x00, 0x00, 0xFF };
    };

    constexpr usize      headerSize = 14;
    constexpr ByteArr<8> endMarker  = toBytes({ 0, 0, 0, 0, 0, 0, 0, 1 });

    template <Channels Chan>
    static constexpr Pixel<Chan> start = StartPixel<Chan>::value;

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

    template <typename T, usize N>
        requires(sizeof(T) == 1)
    constexpr void writeArray(ByteVec& vec, usize& index, const std::array<T, N>& arr) noexcept
    {
        for (const auto& value : arr) {
            vec[index++] = static_cast<Byte>(value);
        }
    }

    void write32(ByteVec& vec, usize& index, u32 value) noexcept
    {
        auto bytes = std::bit_cast<ByteArr<4>>(toBigEndian(value));
        writeArray(vec, index, bytes);
    }

    struct QoiHeader
    {
        std::array<char, 4> m_magic = { 'q', 'o', 'i', 'f' };

        u32 m_width;
        u32 m_height;
        u8  m_channels;
        u8  m_colorspace;

        void write(ByteVec& vec, usize& index) const noexcept
        {
            writeArray(vec, index, m_magic);
            write32(vec, index, m_width);
            write32(vec, index, m_height);

            vec[index++] = static_cast<Byte>(m_channels);
            vec[index++] = static_cast<Byte>(m_colorspace);
        }
    };
    static_assert(DataChunkVec<QoiHeader>);

    struct EndMarker
    {
        static void write(ByteVec& vec, usize& index) noexcept
        {
            writeArray(vec, index, constants::endMarker);
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

            void write(ByteVec& vec, usize& index) const noexcept
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

            void write(ByteVec& vec, usize& index) const noexcept
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

            void write(ByteVec& vec, usize& index) const noexcept
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

            void write(ByteVec& vec, usize& index) const noexcept
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

            void write(ByteVec& vec, usize& index) const noexcept
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

            void write(ByteVec& vec, usize& index) const noexcept
            {
                vec[index++] = static_cast<Byte>(OP_RUN | (m_run + constants::biasOpRun));
            }
        };
        static_assert(DataChunkVec<Run>);

        template <typename T>
        concept Op = AnyOf<T, Rgb, Rgba, Index, Diff, Luma, Run>;

        bool shouldDiff(i8 dr, i8 dg, i8 db) noexcept
        {
            return dr >= constants::minDiff && dr <= constants::maxDiff    //
                && dg >= constants::minDiff && dg <= constants::maxDiff    //
                && db >= constants::minDiff && db <= constants::maxDiff;
        }

        bool shouldLuma(i8 dg, i8 dr_dg, i8 db_dg) noexcept
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
        [[gnu::always_inline]]
        void push(T&& t)
        {
#ifdef QOIPP_DEBUG
            m_opCount[typeid(T).name()]++;
#endif
            t.write(m_bytes, m_index);
        }

        ByteVec get()
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

    template <Channels Chan>
    class RunningArray
    {
    public:
        constexpr static usize s_size = 64;

        // returns index if color match on the same index
        std::optional<usize> put(const Pixel<Chan>& pixel)
        {
            const usize index = [&] {
                if constexpr (Chan == Channels::RGBA) {
                    const auto& [r, g, b, a] = pixel;
                    return (r * 3 + g * 5 + b * 7 + a * 11) % s_size;
                } else {
                    const auto& [r, g, b] = pixel;
                    constexpr auto a      = constants::start<Channels::RGBA>.m_a;
                    return (r * 3 + g * 5 + b * 7 + a * 11) % s_size;
                }
            }();

            auto& oldPixel = m_data[index];
            if (oldPixel == pixel) {
                return index;
            } else {
                oldPixel = pixel;
                return std::nullopt;
            }
        }

    private:
        std::array<Pixel<Chan>, s_size> m_data = {};
    };

    template <Channels Chan>
    class Encoder
    {
    public:
        Encoder(std::span<const Byte> data)
            : m_data{ data }
        {
        }

        ByteVec encode(u32 width, u32 height)
        {
            const usize maxSize = width * height * static_cast<usize>(Chan)    //
                                + constants::headerSize                        //
                                + constants::endMarker.size();

            DataChunkArray chunks{ maxSize };    // the encoded data

            // TODO: correct colorspace field
            chunks.push(data::QoiHeader{
                .m_width      = static_cast<u32>(width),
                .m_height     = static_cast<u32>(height),
                .m_channels   = static_cast<u8>(Chan),
                .m_colorspace = 0,    // 0: sRGB with linear alpha, 1: all channels linear
                                      // TBH, I don't know what this means
            });

            auto previousPixel = constants::start<Chan>;
            i32  lastRun       = 0;

            for (const auto idx : sv::iota(0u, static_cast<usize>(width * height))) {
                const auto& currentPixel = getPixel(idx);

                if (previousPixel == currentPixel) {
                    lastRun++;

                    const bool runLimit  = lastRun == constants::runLimit;
                    const bool lastPixel = idx == static_cast<usize>(width * height) - 1;
                    if (runLimit || lastPixel) {
                        chunks.push(data::op::Run{ .m_run = static_cast<i8>(lastRun) });
                        lastRun = 0;
                    }

                    previousPixel = currentPixel;
                    continue;
                } else if (lastRun > 0) {
                    // ends of OP_RUN
                    chunks.push(data::op::Run{ .m_run = static_cast<i8>(lastRun) });
                    lastRun = 0;
                }

                // OP_INDEX
                if (auto idx = m_runningArray.put(currentPixel); idx.has_value()) {
                    chunks.push(data::op::Index{
                        .m_index = static_cast<u8>(idx.value()),
                    });

                    previousPixel = currentPixel;
                    continue;
                }

                const auto sameAlpha = [&] {
                    if constexpr (Chan == Channels::RGB) {
                        return true;
                    } else {
                        return previousPixel.m_a == currentPixel.m_a;
                    }
                }();

                // OP_DIFF and OP_LUMA
                if (sameAlpha) {
                    const i8 dr = currentPixel.m_r - previousPixel.m_r;
                    const i8 dg = currentPixel.m_g - previousPixel.m_g;
                    const i8 db = currentPixel.m_b - previousPixel.m_b;

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
                            .m_r = currentPixel.m_r,
                            .m_g = currentPixel.m_g,
                            .m_b = currentPixel.m_b,
                        });
                    }
                } else if constexpr (Chan == Channels::RGBA) {
                    // OP_RGBA
                    chunks.push(data::op::Rgba{
                        .m_r = currentPixel.m_r,
                        .m_g = currentPixel.m_g,
                        .m_b = currentPixel.m_b,
                        .m_a = currentPixel.m_a,
                    });
                }

                previousPixel = currentPixel;
            }

            chunks.push(data::EndMarker{});

            return chunks.get();
        }

    private:
        std::span<const Byte> m_data;
        RunningArray<Chan>    m_runningArray;

        const Pixel<Chan>& getPixel(usize index)
        {
            const usize dataIndex = index * static_cast<u32>(Chan);
            return reinterpret_cast<const Pixel<Chan>&>(m_data[dataIndex]);
        }
    };

    // TODO: implement
    class Decoder
    {
    };
}

namespace qoipp
{
    std::vector<Byte> encode(std::span<const Byte> data, ImageDesc desc) noexcept(false)
    {
        const auto [width, height, channels] = desc;
        const auto maxSize                   = static_cast<usize>(width * height * channels);

        if (width <= 0 || height <= 0 || channels <= 0) {
            throw std::invalid_argument(std::format(
                "Invalid image description: w = {}, h = {}, c = {}", width, height, channels
            ));
        }

        if (channels != 3 && channels != 4) {
            throw std::invalid_argument(
                std::format("Invalid number of channels: expected 3 or 4, got {}", channels)
            );
        }

        if (data.size() != maxSize) {
            throw std::invalid_argument(std::format(
                "Data size does not match the image description: expected {}x{}x{} = {}, got {}",
                width,
                height,
                channels,
                maxSize,
                data.size()
            ));
        }

        if (desc.m_channels == 3) {
            impl::Encoder<Channels::RGB> encoder{ data };
            return encoder.encode(static_cast<u32>(width), static_cast<u32>(height));
        } else {
            impl::Encoder<Channels::RGBA> encoder{ data };
            return encoder.encode(static_cast<u32>(width), static_cast<u32>(height));
        }
    }

    // TODO: implement
    QoiImage decode(ByteSpan data) noexcept(false)
    {
        throw std::runtime_error{ "Not implemented yet" };
    }
}
