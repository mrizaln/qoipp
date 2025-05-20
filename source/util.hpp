#ifndef QOIPP_UTIL_HPP_XGTYIRU3W5F4N
#define QOIPP_UTIL_HPP_XGTYIRU3W5F4N

#include "qoipp/common.hpp"
#include <cstring>

namespace qoipp::inline aliases
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
}

namespace qoipp::constants
{
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

    constexpr ByteArr<8> end_marker = { 0, 0, 0, 0, 0, 0, 0, 1 };
    constexpr Pixel      start      = { 0x00, 0x00, 0x00, 0xFF };
}

namespace qoipp::util
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
        { t.write(idx, byte) } -> std::same_as<void>;
        { ct.is_ok(idx) } -> std::same_as<bool>;
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

    template <ByteWriter Out, bool Checked>
    class ChunkArray
    {
    public:
        ChunkArray(Out& out)
            : m_out{ out }
        {
        }

        void write_header(u32 width, u32 height, Channels channels, Colorspace colorspace) noexcept
        {
            if constexpr (Checked) {
                if (not can_write(m_index + constants::header_size - 1)) {
                    m_ok = false;
                    return;
                }
            }

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
            if constexpr (Checked) {
                if (not can_write(m_index + constants::end_marker.size() - 1)) {
                    m_ok = false;
                    return;
                }
            }
            for (auto byte : constants::end_marker) {
                m_out.write(m_index++, byte);
            }
        }

        void write_rgb(const Pixel& pixel) noexcept
        {
            if constexpr (Checked) {
                if (not can_write(m_index + 4 - 1)) {
                    m_ok = false;
                    return;
                }
            }
            m_out.write(m_index++, Tag::OP_RGB);
            m_out.write(m_index++, pixel.r);
            m_out.write(m_index++, pixel.g);
            m_out.write(m_index++, pixel.b);
        }

        void write_rgba(const Pixel& pixel) noexcept
        {
            if constexpr (Checked) {
                if (not can_write(m_index + 5 - 1)) {
                    m_ok = false;
                    return;
                }
            }
            m_out.write(m_index++, Tag::OP_RGBA);
            m_out.write(m_index++, pixel.r);
            m_out.write(m_index++, pixel.g);
            m_out.write(m_index++, pixel.b);
            m_out.write(m_index++, pixel.a);
        }

        void write_index(u8 index) noexcept
        {
            if constexpr (Checked) {
                if (not can_write(m_index)) {
                    m_ok = false;
                    return;
                }
            }
            m_out.write(m_index++, Tag::OP_INDEX | index);    //
        }

        void write_diff(i8 dr, i8 dg, i8 db) noexcept
        {
            if constexpr (Checked) {
                if (not can_write(m_index)) {
                    m_ok = false;
                    return;
                }
            }
            constexpr auto bias = constants::bias_op_diff;

            const auto val = Tag::OP_DIFF | (dr + bias) << 4 | (dg + bias) << 2 | (db + bias);
            m_out.write(m_index++, static_cast<u8>(val));
        }

        void write_luma(i8 dg, i8 dr_dg, i8 db_dg) noexcept
        {
            if constexpr (Checked) {
                if (not can_write(m_index + 2 - 1)) {
                    m_ok = false;
                    return;
                }
            }
            constexpr auto bias_g  = constants::bias_op_luma_g;
            constexpr auto bias_rb = constants::bias_op_luma_rb;

            m_out.write(m_index++, static_cast<u8>(Tag::OP_LUMA | (dg + bias_g)));
            m_out.write(m_index++, static_cast<u8>((dr_dg + bias_rb) << 4 | (db_dg + bias_rb)));
        }

        void write_run(u8 run) noexcept
        {
            if constexpr (Checked) {
                if (not can_write(m_index)) {
                    return;
                }
            }
            m_out.write(m_index++, static_cast<u8>(Tag::OP_RUN | (run + constants::bias_op_run)));
        }

        usize count() const noexcept { return m_index; }
        bool  ok() { return m_ok; }

        bool can_write(usize index)
        {
            if (not m_ok or not m_out.is_ok(index)) {
                return m_ok = false;
            }
            return true;
        }

    private:
        Out&  m_out;
        usize m_index = 0;
        bool  m_ok    = true;
    };

    struct SimpleByteWriter
    {
        ByteSpan dest;

        void write(usize index, u8 byte) { dest[index] = byte; }
        bool is_ok(usize index) const { return index < dest.size(); }
    };
    static_assert(ByteWriter<SimpleByteWriter>);

    struct FuncByteWriter
    {
        ByteSinkFun func;

        void write([[maybe_unused]] usize index, u8 byte) noexcept { func(byte); }
        bool is_ok(usize) const { return true; }
    };
    static_assert(ByteWriter<FuncByteWriter>);

    template <bool Checked>
    struct SimplePixelWriter
    {
        static constexpr bool is_checked = Checked;

        ByteSpan dest;
        Channels channels;
        bool     out_of_bound = false;

        void write(usize index, const Pixel& pixel) noexcept
        {
            const auto offset = index * static_cast<usize>(channels);
            if constexpr (Checked) {
                if (offset >= dest.size()) {
                    out_of_bound = true;
                    return;
                }
            }

            if (channels == Channels::RGBA) {
                std::memcpy(dest.data() + offset, &pixel, 4);
            } else {
                std::memcpy(dest.data() + offset, &pixel, 3);
            }
        }

        bool ok() const { return not out_of_bound; }
    };
    static_assert(PixelWriter<SimplePixelWriter<false>>);
    static_assert(PixelWriter<SimplePixelWriter<true>>);

    struct FuncPixelWriter
    {
        static constexpr bool is_checked = false;

        PixelSinkFun func;

        void write([[maybe_unused]] usize index, const Pixel& pixel) noexcept { func(pixel); }
        bool ok() const { return true; }
    };
    static_assert(PixelWriter<FuncPixelWriter>);

    struct SimplePixelReader
    {
        ByteCSpan data;
        Channels  channels;

        Pixel read(usize index) const noexcept
        {
            const auto offset = index * static_cast<usize>(channels);
            if (channels == Channels::RGBA) {
                return { data[offset + 0], data[offset + 1], data[offset + 2], data[offset + 3] };
            } else {
                return { data[offset + 0], data[offset + 1], data[offset + 2], 0xFF };
            }
        }
    };
    static_assert(PixelReader<SimplePixelReader>);

    struct FuncPixelReader
    {
        PixelGenFun func;
        Channels    channels;

        Pixel read(usize index) const noexcept
        {
            auto pixel = func(index);
            if (channels == Channels::RGB) {
                pixel.a = 0xFF;
            }
            return pixel;
        }
    };
    static_assert(PixelReader<FuncPixelReader>);

    inline usize hash(const Pixel& pixel)
    {
        const auto& [r, g, b, a] = pixel;
        return (r * 3 + g * 5 + b * 7 + a * 11);
    }
}

#endif
