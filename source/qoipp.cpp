#include "qoipp.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <format>
#include <iterator>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

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
        auto bytes = std::bit_cast<ByteArr<sizeof(T)>>(value);
        sr::reverse(bytes);
        return std::bit_cast<T>(bytes);
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

    consteval Pixel<Channels::RGBA> operator""_rgba(unsigned long long value)
    {
        if (value > 0xFFFFFFFF) {
            throw std::invalid_argument("value is too large");
        }

        // truncate to 32 bits
        u32 value32 = static_cast<u32>(value);

        const u8 r = (value32 >> 24) & 0xFF;
        const u8 g = (value32 >> 16) & 0xFF;
        const u8 b = (value32 >> 8) & 0xFF;
        const u8 a = value32 & 0xFF;

        return { r, g, b, a };
    }

    consteval Pixel<Channels::RGB> operator""_rgb(unsigned long long value)
    {
        if (value > 0xFFFFFF) {
            throw std::invalid_argument("value is too large");
        }

        // truncate to 32 bits
        u32 value32 = static_cast<u32>(value);

        const u8 r = (value32 >> 16) & 0xFF;
        const u8 g = (value32 >> 8) & 0xFF;
        const u8 b = value32 & 0xFF;

        return { r, g, b };
    }
}

namespace qoipp::constants
{
    template <Channels>
    struct StartPixel;

    template <>
    struct StartPixel<Channels::RGB>
    {
        constexpr static Pixel<Channels::RGB> value = 0x000000_rgb;
    };

    template <>
    struct StartPixel<Channels::RGBA>
    {
        constexpr static Pixel<Channels::RGBA> value = 0x000000FF_rgba;
    };

    constexpr ByteArr<8> endMarker = toBytes({ 0, 0, 0, 0, 0, 0, 0, 1 });

    template <Channels Chan>
    static constexpr Pixel<Chan> start = StartPixel<Chan>::value;

    constexpr i8 biasOpRun    = -1;
    constexpr i8 biasOpDiff   = 2;
    constexpr i8 biasOpLumaG  = 32;
    constexpr i8 biasOpLumaRB = 8;

    constexpr i8 runLimit = 62;
}

namespace qoipp::data
{
    // clang-format off
    template <typename T>
    concept DataChunk = requires(T t) { { t.toBytes() } -> ByteRange; };
    // clang-format on

    struct QoiHeader
    {
        char m_magic[4] = { 'q', 'o', 'i', 'f' };
        u32  m_width;
        u32  m_height;
        u8   m_channels;
        u8   m_colorspace;

        ByteArr<14> toBytes() const
        {
            ByteArr<14> result;

            result[0] = static_cast<Byte>(m_magic[0]);
            result[1] = static_cast<Byte>(m_magic[1]);
            result[2] = static_cast<Byte>(m_magic[2]);
            result[3] = static_cast<Byte>(m_magic[3]);

            auto widthBytes = std::bit_cast<ByteArr<4>>(toBigEndian(m_width));
            result[4]       = widthBytes[0];
            result[5]       = widthBytes[1];
            result[6]       = widthBytes[2];
            result[7]       = widthBytes[3];

            auto heightBytes = std::bit_cast<ByteArr<4>>(toBigEndian(m_height));
            result[8]        = heightBytes[0];
            result[9]        = heightBytes[1];
            result[10]       = heightBytes[2];
            result[11]       = heightBytes[3];

            result[12] = static_cast<Byte>(m_channels);
            result[13] = static_cast<Byte>(m_colorspace);

            return result;
        }
    };
    static_assert(DataChunk<QoiHeader>);

    struct EndMarker
    {
        constexpr ByteArr<8> toBytes() { return constants::endMarker; }
    };
    static_assert(DataChunk<EndMarker>);

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

            ByteArr<4> toBytes() const
            {
                return {
                    static_cast<Byte>(OP_RGB),
                    static_cast<Byte>(m_r),
                    static_cast<Byte>(m_g),
                    static_cast<Byte>(m_b),
                };
            }
        };
        static_assert(DataChunk<Rgb>);

        struct Rgba
        {
            u8 m_r = 0;
            u8 m_g = 0;
            u8 m_b = 0;
            u8 m_a = 0;

            ByteArr<5> toBytes() const
            {
                return {
                    static_cast<Byte>(OP_RGBA), static_cast<Byte>(m_r), static_cast<Byte>(m_g),
                    static_cast<Byte>(m_b),     static_cast<Byte>(m_a),
                };
            }
        };
        static_assert(DataChunk<Rgba>);

        struct Index
        {
            u8 m_index = 0;

            ByteArr<1> toBytes() const { return { static_cast<Byte>(OP_INDEX | m_index) }; }
        };
        static_assert(DataChunk<Index>);

        struct Diff
        {
            i8 m_dr = 0;
            i8 m_dg = 0;
            i8 m_db = 0;

            ByteArr<1> toBytes() const
            {
                return { static_cast<Byte>(OP_DIFF | m_dr << 4 | m_dg << 2 | m_db) };
            }
        };
        static_assert(DataChunk<Diff>);

        struct Luma
        {
            i8 m_dg    = 0;
            i8 m_dr_dg = 0;
            i8 m_db_dg = 0;

            ByteArr<2> toBytes() const
            {
                return {
                    static_cast<Byte>(OP_LUMA | m_dg),
                    static_cast<Byte>(m_dr_dg << 4 | m_db_dg),
                };
            }
        };
        static_assert(DataChunk<Luma>);

        struct Run
        {
            u8 m_run = 0;

            ByteArr<1> toBytes() const { return { static_cast<Byte>(OP_RUN | m_run) }; }
        };
        static_assert(DataChunk<Run>);

        template <typename T>
        concept Op = AnyOf<T, Rgb, Rgba, Index, Diff, Luma, Run>;
    }
}

namespace qoipp::impl
{
    class DataChunkArray
    {
    public:
        template <typename T>
            requires(data::op::Op<T> or AnyOf<T, data::QoiHeader, data::EndMarker>)
        void push(T&& t)
        {
            sr::move(t.toBytes(), std::back_inserter(m_bytes));
        }

        void      fit() { m_bytes.shrink_to_fit(); }
        void      reserve(usize size) { m_bytes.reserve(size); }
        ByteVec&& get() { return std::move(m_bytes); }

    private:
        ByteVec m_bytes;
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
            , m_chunks{}
        {
        }

        ByteVec encode(u32 width, u32 height)
        {
            m_chunks.reserve(width * height * static_cast<usize>(Chan));

            // TODO: correct colorspace field
            m_chunks.push(data::QoiHeader{
                .m_width      = static_cast<u32>(width),
                .m_height     = static_cast<u32>(height),
                .m_channels   = static_cast<u8>(Chan),
                .m_colorspace = 0,    // 0: sRGB with linear alpha, 1: all channels linear
                                      // TBH, I don't know what this means
            });

            const auto* previousPixel = &constants::start<Chan>;
            i32         lastRun       = 0;

            for (const auto idx : sv::iota(0u, static_cast<usize>(width * height))) {
                const auto* currentPixel = getPixel(idx);

                // OP_RUN
                if (*previousPixel == *currentPixel) {
                    lastRun++;

                    const bool runLimit  = lastRun == constants::runLimit;
                    const bool lastPixel = idx == static_cast<usize>(width * height) - 1;
                    if (runLimit || lastPixel) {
                        m_chunks.push(data::op::Run{
                            .m_run = static_cast<u8>(lastRun + constants::biasOpRun),
                        });
                        lastRun = 0;
                    }

                    previousPixel = currentPixel;
                    continue;

                } else if (lastRun > 0) {
                    // ends of OP_RUN
                    m_chunks.push(data::op::Run{
                        .m_run = static_cast<u8>(lastRun + constants::biasOpRun),
                    });
                    lastRun = 0;
                }

                // OP_INDEX
                if (auto idx = m_runningArray.put(*currentPixel); idx.has_value()) {
                    m_chunks.push(data::op::Index{
                        .m_index = static_cast<u8>(idx.value()),
                    });

                    previousPixel = currentPixel;
                    continue;
                }

                const auto sameAlpha = [&] {
                    if constexpr (Chan == Channels::RGB) {
                        return true;
                    } else {
                        return previousPixel->m_a == currentPixel->m_a;
                    }
                }();

                // OP_DIFF and OP_LUMA
                if (sameAlpha) {
                    const i8 dr = currentPixel->m_r - previousPixel->m_r;
                    const i8 dg = currentPixel->m_g - previousPixel->m_g;
                    const i8 db = currentPixel->m_b - previousPixel->m_b;

                    const i8 dr_dg = dr - dg;
                    const i8 db_dg = db - dg;

                    if (                            //
                        (dr >= -2 && dr <= 1) &&    //
                        (dg >= -2 && dg <= 1) &&    //
                        (db >= -2 && db <= 1)       //
                    ) {
                        m_chunks.push(data::op::Diff{
                            .m_dr = static_cast<i8>(dr + constants::biasOpDiff),
                            .m_dg = static_cast<i8>(dg + constants::biasOpDiff),
                            .m_db = static_cast<i8>(db + constants::biasOpDiff),
                        });
                    } else if (                           //
                        (dr_dg >= -8 && dr_dg <= 7) &&    //
                        (db_dg >= -8 && db_dg <= 7) &&    //
                        (dg >= -32 && dg <= 31)           //
                    ) {
                        m_chunks.push(data::op::Luma{
                            .m_dg    = static_cast<i8>(dg + constants::biasOpLumaG),
                            .m_dr_dg = static_cast<i8>(dr_dg + constants::biasOpLumaRB),
                            .m_db_dg = static_cast<i8>(db_dg + constants::biasOpLumaRB),
                        });
                    } else {
                        m_chunks.push(data::op::Rgb{
                            .m_r = currentPixel->m_r,
                            .m_g = currentPixel->m_g,
                            .m_b = currentPixel->m_b,
                        });
                    }

                    previousPixel = currentPixel;
                    continue;
                }

                // OP_RGBA
                if constexpr (Chan == Channels::RGBA) {
                    m_chunks.push(data::op::Rgba{
                        .m_r = currentPixel->m_r,
                        .m_g = currentPixel->m_g,
                        .m_b = currentPixel->m_b,
                        .m_a = currentPixel->m_a,
                    });
                }

                previousPixel = currentPixel;
            }

            m_chunks.push(data::EndMarker{});

            // m_chunks.fit();

            return m_chunks.get();
        }

    private:
        std::span<const Byte> m_data;
        DataChunkArray        m_chunks;    // the encoded data
        RunningArray<Chan>    m_runningArray;

        const Pixel<Chan>* getPixel(usize index)
        {
            const usize dataIndex = index * static_cast<u32>(Chan);
            return reinterpret_cast<const Pixel<Chan>*>(&m_data[dataIndex]);
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
