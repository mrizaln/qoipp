#ifndef QOIPP_UTIL_HPP_XGTYIRU3W5F4N
#define QOIPP_UTIL_HPP_XGTYIRU3W5F4N

#include "qoipp/common.hpp"

// utils and aliases
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

    template <usize N>
    using ByteArr = std::array<u8, N>;

    template <typename T, typename... Ts>
    concept AnyOf = (std::same_as<T, Ts> or ...);

    template <class T>
    std::decay_t<T> decay_copy(T&&);

    template <typename T>
    concept PixelReader = requires (const T ct, usize idx) {
        { ct.read(idx) } -> std::same_as<Pixel>;
    };

    template <typename T>
    concept PixelWriter = requires (const T ct, T t, usize idx, const Pixel& pixel) {
        { decay_copy(T::is_checked) } -> std::same_as<bool>;    // NOTE: must also be constexpr

        { t.write(idx, pixel) } -> std::same_as<void>;
        { ct.ok() } -> std::same_as<bool>;
    };

    template <typename T>
    concept ByteReader = requires (const T ct, usize idx) {
        { ct.read(idx) } -> std::same_as<u8>;
    };

    template <typename T>
    concept ByteWriter = requires (const T ct, T t, usize idx, u8 byte) {
        { decay_copy(T::is_checked) } -> std::same_as<bool>;    // NOTE: must also be constexpr

        { t.write(idx, byte) } -> std::same_as<void>;
        { ct.ok() } -> std::same_as<bool>;
    };

    template <std::integral T>
    constexpr T to_native_endian(const T& value) noexcept
    {
        if constexpr (std::endian::native == std::endian::big) {
            return value;
        } else {
            constexpr auto size  = sizeof(T);
            auto           array = std::bit_cast<std::array<std::byte, size>>(value);
            for (auto i = 0u; i < size / 2; ++i) {
                std::swap(array[i], array[size - i - 1]);
            }
            return std::bit_cast<T>(array);
        }
    }

    template <typename T, typename... Args>
    Result<T> make_result(Args&&... args)
    {
#if defined(__cpp_lib_expected)
        return Result<T>{ std::in_place, std::forward<Args>(args)... };
#else
        return Result<T>{ std::forward<Args>(args)... };
#endif
    }

    template <typename T>
    Result<T> make_error(Error error)
    {
#if defined(__cpp_lib_expected)
        return Result<T>{ std::unexpect, error };
#else
        return Result<T>{ error };
#endif
    }

    inline bool desc_is_valid(const Desc& desc)
    {
        const auto& [width, height, channels, colorspace] = desc;
        return width > 0 and height > 0    //
           and (channels == Channels::RGBA or channels == Channels::RGB)
           and (colorspace == Colorspace::Linear or colorspace == Colorspace::sRGB);
    }

    inline std::optional<usize> count_bytes(const Desc& desc)
    {
        // detect overflow: https://stackoverflow.com/a/1815371/16506263
        auto overflows = [](usize a, usize b) {
            const auto c = a * b;
            return a != 0 and c / a != b;
        };

        const auto& [width, height, channels, _] = desc;
        if (overflows(width, height)) {
            return std::nullopt;
        }

        const auto pixel_count = static_cast<usize>(width) * height;
        const auto chan        = static_cast<usize>(channels);
        if (overflows(pixel_count, chan)) {
            return std::nullopt;
        }

        return pixel_count * chan;
    }
}

namespace qoipp::constants
{
    constexpr std::array magic = { 'q', 'o', 'i', 'f' };

    constexpr usize      header_size        = 14;
    constexpr usize      running_array_size = 64;
    constexpr ByteArr<8> end_marker         = { 0, 0, 0, 0, 0, 0, 0, 1 };

    constexpr i8 bias_op_run     = -1;
    constexpr i8 bias_op_diff    = 2;
    constexpr i8 bias_op_luma_g  = 32;
    constexpr i8 bias_op_luma_rb = 8;
    constexpr i8 run_limit       = 62;
    constexpr i8 min_diff        = -2;
    constexpr i8 max_diff        = 1;
    constexpr i8 min_luma_g      = -32;
    constexpr i8 max_luma_g      = 31;
    constexpr i8 min_luma_rb     = -8;
    constexpr i8 max_luma_rb     = 7;

    constexpr Pixel start = { 0x00, 0x00, 0x00, 0xFF };
}

namespace qoipp::op
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

    inline bool should_diff(i8 dr, i8 dg, i8 db) noexcept
    {
        return dr >= constants::min_diff and dr <= constants::max_diff    //
           and dg >= constants::min_diff and dg <= constants::max_diff    //
           and db >= constants::min_diff and db <= constants::max_diff;
    }

    inline bool should_luma(i8 dg, i8 dr_dg, i8 db_dg) noexcept
    {
        return dr_dg >= constants::min_luma_rb and dr_dg <= constants::max_luma_rb    //
           and db_dg >= constants::min_luma_rb and db_dg <= constants::max_luma_rb    //
           and dg >= constants::min_luma_g and dg <= constants::max_luma_g;
    }

    template <ByteWriter Out>
    class ChunkArray
    {
    public:
        ChunkArray(Out& out)
            : m_out{ out }
        {
        }

        void write_header(u32 width, u32 height, Channels channels, Colorspace colorspace) noexcept
        {
            for (char c : constants::magic) {
                m_out.write(m_index++, static_cast<u8>(c));
            }

            auto width_arr = std::bit_cast<ByteArr<4>>(width);
            for (auto i = 4u; i > 0; --i) {
                m_out.write(m_index++, width_arr[i - 1]);
            }

            auto height_arr = std::bit_cast<ByteArr<4>>(height);
            for (auto i = 4u; i > 0; --i) {
                m_out.write(m_index++, height_arr[i - 1]);
            }

            m_out.write(m_index++, static_cast<u8>(channels));
            m_out.write(m_index++, static_cast<u8>(colorspace));
        }

        void write_end_marker() noexcept
        {
            for (auto byte : constants::end_marker) {
                m_out.write(m_index++, byte);
            }
        }

        void write_rgb(const Pixel& pixel) noexcept
        {
            m_out.write(m_index++, Tag::OP_RGB);
            m_out.write(m_index++, pixel.r);
            m_out.write(m_index++, pixel.g);
            m_out.write(m_index++, pixel.b);
        }

        void write_rgba(const Pixel& pixel) noexcept
        {
            m_out.write(m_index++, Tag::OP_RGBA);
            m_out.write(m_index++, pixel.r);
            m_out.write(m_index++, pixel.g);
            m_out.write(m_index++, pixel.b);
            m_out.write(m_index++, pixel.a);
        }

        void write_index(u8 index) noexcept
        {
            m_out.write(m_index++, Tag::OP_INDEX | index);    //
        }

        void write_diff(i8 dr, i8 dg, i8 db) noexcept
        {
            constexpr auto bias = constants::bias_op_diff;

            const auto val = Tag::OP_DIFF | (dr + bias) << 4 | (dg + bias) << 2 | (db + bias);
            m_out.write(m_index++, static_cast<u8>(val));
        }

        void write_luma(i8 dg, i8 dr_dg, i8 db_dg) noexcept
        {
            constexpr auto bias_g  = constants::bias_op_luma_g;
            constexpr auto bias_rb = constants::bias_op_luma_rb;

            m_out.write(m_index++, static_cast<u8>(Tag::OP_LUMA | (dg + bias_g)));
            m_out.write(m_index++, static_cast<u8>((dr_dg + bias_rb) << 4 | (db_dg + bias_rb)));
        }

        void write_run(u8 run) noexcept
        {
            m_out.write(m_index++, static_cast<u8>(Tag::OP_RUN | (run + constants::bias_op_run)));
        }

        usize count() const noexcept { return m_index; }

    private:
        Out&  m_out;
        usize m_index = 0;
    };
}

#endif
