#include "qoipp/qoipp.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include <utility>

// utils and aliases
namespace qoipp
{
    namespace fs = std::filesystem;

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
    using Arr = std::array<u8, N>;

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

    bool desc_is_valid(const Desc& desc)
    {
        const auto& [width, height, channels, colorspace] = desc;
        return width > 0 and height > 0    //
           and (channels == Channels::RGBA or channels == Channels::RGB)
           and (colorspace == Colorspace::Linear or colorspace == Colorspace::sRGB);
    }

    std::optional<usize> count_bytes(const Desc& desc)
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

    constexpr usize  header_size        = 14;
    constexpr usize  running_array_size = 64;
    constexpr Arr<8> end_marker         = { 0, 0, 0, 0, 0, 0, 0, 1 };

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

            auto width_arr = std::bit_cast<Arr<4>>(width);
            for (auto i = 4u; i > 0; --i) {
                m_out.write(m_index++, width_arr[i - 1]);
            }

            auto height_arr = std::bit_cast<Arr<4>>(height);
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

namespace qoipp::impl
{
    using RunningArray = std::array<Pixel, constants::running_array_size>;

    template <bool Checked>
    struct SimpleByteWriter
    {
        static constexpr bool is_checked = Checked;

        ByteSpan dest;
        bool     out_of_bound = false;

        void write(usize index, u8 byte)
        {
            if constexpr (Checked) {
                if (index >= dest.size()) {
                    out_of_bound = true;
                    return;
                }
            }
            dest[index] = byte;
        }

        bool ok() const { return not out_of_bound; }
    };
    static_assert(ByteWriter<SimpleByteWriter<false>>);
    static_assert(ByteWriter<SimpleByteWriter<true>>);

    struct FuncByteWriter
    {
        static constexpr bool is_checked = false;

        ByteSinkFun func;

        void write([[maybe_unused]] usize index, u8 byte) noexcept { func(byte); }
        bool ok() const { return true; }
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

    // TODO: maybe add the unchecked template parameter here instead of on SimpleByteWriter
    template <PixelReader In, ByteWriter Out>
    std::optional<usize> encode(
        Out        out,
        In         in,
        u32        width,
        u32        height,
        Channels   channels,
        Colorspace colorspace
    ) noexcept
    {
        auto chunks      = op::ChunkArray{ out };    // the encoded data goes here
        auto seen_pixels = RunningArray{};

        chunks.write_header(width, height, channels, colorspace);

        auto prev_pixel = constants::start;
        auto curr_pixel = constants::start;
        u8   run        = 0;

        for (usize pixel_index = 0u; pixel_index < width * height; ++pixel_index) {
            curr_pixel = in.read(pixel_index);

            if (prev_pixel == curr_pixel) {
                ++run;
                if (run == constants::run_limit) {
                    chunks.write_run(run);
                    run = 0;
                }
            } else {
                if (run > 0) {
                    chunks.write_run(run);
                    run = 0;
                }

                const u8 index = hash(curr_pixel) % constants::running_array_size;

                // OP_INDEX
                if (seen_pixels[index] == curr_pixel) {
                    chunks.write_index(index);
                } else {
                    seen_pixels[index] = curr_pixel;

                    if (in.channels == Channels::RGBA and prev_pixel.a != curr_pixel.a) {
                        chunks.write_rgba(curr_pixel);
                        prev_pixel = curr_pixel;
                        continue;
                    }

                    // OP_DIFF and OP_LUMA
                    const i8 dr = curr_pixel.r - prev_pixel.r;
                    const i8 dg = curr_pixel.g - prev_pixel.g;
                    const i8 db = curr_pixel.b - prev_pixel.b;

                    const i8 dr_dg = dr - dg;
                    const i8 db_dg = db - dg;

                    if (op::should_diff(dr, dg, db)) {
                        chunks.write_diff(dr, dg, db);
                    } else if (op::should_luma(dg, dr_dg, db_dg)) {
                        chunks.write_luma(dg, dr_dg, db_dg);
                    } else {
                        chunks.write_rgb(curr_pixel);
                    }
                }
            }

            prev_pixel = curr_pixel;
            if constexpr (Out::is_checked) {
                if (not out.ok()) {
                    return std::nullopt;
                }
            }
        }

        if (run > 0) {
            chunks.write_run(run);
            run = 0;
        }
        chunks.write_end_marker();

        return out.ok() ? std::optional{ chunks.count() } : std::nullopt;
    }

    template <PixelWriter Out>
    void decode(Out out, ByteCSpan in, Channels channels, usize width, usize height) noexcept(false)
    {
        auto seen_pixels = RunningArray{};
        auto prev_pixel  = constants::start;

        const auto get = [&](usize index) -> u8 { return index < in.size() ? in[index] : 0x00; };

        seen_pixels[hash(prev_pixel) % constants::running_array_size] = prev_pixel;

        const auto chunks_size = in.size() - constants::header_size - constants::end_marker.size();
        for (usize pixel_index = 0, data_index = constants::header_size;
             data_index < chunks_size or pixel_index < width * height;
             ++pixel_index) {

            const auto tag        = get(data_index++);
            auto       curr_pixel = prev_pixel;

            switch (tag) {
            case op::Tag::OP_RGB: {
                curr_pixel.r = get(data_index++);
                curr_pixel.g = get(data_index++);
                curr_pixel.b = get(data_index++);
                if (channels == Channels::RGBA) {
                    curr_pixel.a = prev_pixel.a;
                }
            } break;
            case op::Tag::OP_RGBA: {
                curr_pixel.r = get(data_index++);
                curr_pixel.g = get(data_index++);
                curr_pixel.b = get(data_index++);
                if (channels == Channels::RGBA) {
                    curr_pixel.a = get(data_index++);
                }
            } break;
            default:
                switch (tag & 0b11000000) {
                case op::Tag::OP_INDEX: {
                    auto& pixel = seen_pixels[tag & 0b00111111];
                    curr_pixel  = pixel;
                } break;
                case op::Tag::OP_DIFF: {
                    const i8 dr = ((tag & 0b00110000) >> 4) - constants::bias_op_diff;
                    const i8 dg = ((tag & 0b00001100) >> 2) - constants::bias_op_diff;
                    const i8 db = ((tag & 0b00000011)) - constants::bias_op_diff;

                    curr_pixel.r = static_cast<u8>(dr + prev_pixel.r);
                    curr_pixel.g = static_cast<u8>(dg + prev_pixel.g);
                    curr_pixel.b = static_cast<u8>(db + prev_pixel.b);
                    if (channels == Channels::RGBA) {
                        curr_pixel.a = static_cast<u8>(prev_pixel.a);
                    }
                } break;
                case op::Tag::OP_LUMA: {
                    const auto read_blue = get(data_index++);

                    const u8 dg    = (tag & 0b00111111) - constants::bias_op_luma_g;
                    const u8 dr_dg = ((read_blue & 0b11110000) >> 4) - constants::bias_op_luma_rb;
                    const u8 db_dg = (read_blue & 0b00001111) - constants::bias_op_luma_rb;

                    curr_pixel.r = static_cast<u8>(dg + dr_dg + prev_pixel.r);
                    curr_pixel.g = static_cast<u8>(dg + prev_pixel.g);
                    curr_pixel.b = static_cast<u8>(dg + db_dg + prev_pixel.b);
                    if (channels == Channels::RGBA) {
                        curr_pixel.a = static_cast<u8>(prev_pixel.a);
                    }
                } break;
                case op::Tag::OP_RUN: {
                    auto run = (tag & 0b00111111) - constants::bias_op_run;
                    while (run-- > 0 and pixel_index < width * height) {
                        out.write(pixel_index++, prev_pixel);
                    }
                    --pixel_index;
                    if (pixel_index >= width * height) {
                        break;
                    }
                    continue;
                } break;
                default: [[unlikely]] /* invalid tag (is this even possible?)*/;
                }
            }

            out.write(pixel_index, curr_pixel);
            prev_pixel = seen_pixels[hash(curr_pixel) % constants::running_array_size] = curr_pixel;
        }
    }
}

namespace qoipp
{
    std::string_view to_string(Error err) noexcept
    {
        switch (err) {
        case Error::Empty: return "Data is empty";
        case Error::TooShort: return "Data is too short";
        case Error::TooBig: return "Image is too big to process";
        case Error::NotQoi: return "Not a qoi file";
        case Error::InvalidDesc: return "Image description is invalid";
        case Error::MismatchedDesc: return "Image description does not match the data";
        case Error::NotEnoughSpace: return "Buffer does not have enough space";
        case Error::NotRegularFile: return "Not a regular file";
        case Error::FileExists: return "File already exists";
        case Error::FileNotExists: return "File does not exist";
        case Error::IoError: return "Unable to do read or write operation";
        case Error::BadAlloc: return "Failed to allocate memory";
        }

        return "Unknown";
    }

    Result<Desc> read_header(ByteCSpan in_data) noexcept
    {
        if (in_data.size() == 0) {
            return make_error<Desc>(Error::Empty);
        } else if (in_data.size() < constants::header_size) {
            return make_error<Desc>(Error::TooShort);
        }

        auto magic = std::array<char, constants::magic.size()>{};
        std::memcpy(magic.data(), in_data.data(), magic.size());

        if (magic != constants::magic) {
            return make_error<Desc>(Error::NotQoi);
        }

        auto index = constants::magic.size();

        u32 width, height;

        std::memcpy(&width, in_data.data() + index, sizeof(u32));
        index += sizeof(u32);
        std::memcpy(&height, in_data.data() + index, sizeof(u32));
        index += sizeof(u32);

        auto channels   = to_channels(in_data[index++]);
        auto colorspace = to_colorspace(in_data[index++]);

        if (not channels or not colorspace or width == 0 or height == 0) {
            return make_error<Desc>(Error::InvalidDesc);
        }

        return Desc{
            .width      = to_native_endian(width),
            .height     = to_native_endian(height),
            .channels   = *channels,
            .colorspace = *colorspace,
        };
    }

    Result<Desc> read_header(const fs::path& in_path) noexcept
    {
        if (not fs::exists(in_path)) {
            return make_error<Desc>(Error::FileNotExists);
        } else if (not fs::is_regular_file(in_path)) {
            return make_error<Desc>(Error::NotRegularFile);
        }

        auto file = std::ifstream{ in_path, std::ios::binary };
        if (not file.is_open()) {
            return make_error<Desc>(Error::IoError);
        }

        auto data = Arr<constants::header_size>{};
        file.read(reinterpret_cast<char*>(data.data()), constants::header_size);
        if (not file) {
            return make_error<Desc>(Error::IoError);
        }

        return read_header(data);
    }

    Result<ByteVec> encode(ByteCSpan in_data, Desc desc) noexcept
    {
        const auto [width, height, channels, colorspace] = desc;

        if (in_data.size() == 0) {
            return make_error<ByteVec>(Error::Empty);
        } else if (not desc_is_valid(desc)) {
            return make_error<ByteVec>(Error::InvalidDesc);
        } else if (const auto bytes_count = count_bytes(desc); not bytes_count) {
            return make_error<ByteVec>(Error::TooBig);
        } else if (in_data.size() != bytes_count) {
            return make_error<ByteVec>(Error::MismatchedDesc);
        }

        // worst possible scenario is when no data is compressed + header + end_marker + tag (rgb/rgba)
        const auto worst_size = (static_cast<usize>(channels) + 1) * width * height    // chanels + 1 tag
                              + constants::header_size + constants::end_marker.size();

        auto result = ByteVec{};
        try {
            result = ByteVec(worst_size);
        } catch (...) {
            return make_error<ByteVec>(Error::BadAlloc);
        }

        auto writer = impl::SimpleByteWriter<false>{ result };
        auto reader = impl::SimplePixelReader{ in_data, channels };

        const auto count = impl::encode(writer, reader, width, height, channels, colorspace).value();
        result.resize(count);

        return result;
    }

    Result<ByteVec> encode(PixelGenFun in_func, Desc desc) noexcept
    {
        const auto [width, height, channels, colorspace] = desc;
        if (not desc_is_valid(desc)) {
            return make_error<ByteVec>(Error::InvalidDesc);
        } else if (const auto bytes_count = count_bytes(desc); not bytes_count) {
            return make_error<ByteVec>(Error::TooBig);
        }

        // worst possible scenario is when no data is compressed + header + end_marker + tag (rgb/rgba)
        const auto worst_size = (static_cast<usize>(channels) + 1) * width * height    // OP_RGBA
                              + constants::header_size + constants::end_marker.size();

        auto result = ByteVec{};
        try {
            result = ByteVec(worst_size);
        } catch (...) {
            return make_error<ByteVec>(Error::BadAlloc);
        }

        auto writer = impl::SimpleByteWriter<false>{ result };
        auto reader = impl::FuncPixelReader{ in_func, channels };

        const auto count = impl::encode(writer, reader, width, height, channels, colorspace).value();
        result.resize(count);

        return result;
    }

    Result<usize> encode_into(ByteSpan out_buf, ByteCSpan in_data, Desc desc)
    {
        const auto [width, height, channels, colorspace] = desc;

        if (in_data.size() == 0) {
            return make_error<usize>(Error::Empty);
        } else if (not desc_is_valid(desc)) {
            return make_error<usize>(Error::InvalidDesc);
        } else if (const auto bytes_count = count_bytes(desc); not bytes_count) {
            return make_error<usize>(Error::TooBig);
        } else if (in_data.size() != bytes_count) {
            return make_error<usize>(Error::MismatchedDesc);
        }

        auto writer = impl::SimpleByteWriter<true>{ out_buf };
        auto reader = impl::SimplePixelReader{ in_data, channels };

        const auto count = impl::encode(writer, reader, width, height, channels, colorspace);
        if (not count) {
            return make_error<usize>(Error::NotEnoughSpace);
        }
        return count.value();
    }

    Result<usize> encode_into(ByteSpan out_buf, PixelGenFun in_func, Desc desc)
    {
        const auto [width, height, channels, colorspace] = desc;
        if (not desc_is_valid(desc)) {
            return make_error<usize>(Error::InvalidDesc);
        } else if (const auto bytes_count = count_bytes(desc); not bytes_count) {
            return make_error<usize>(Error::TooBig);
        }

        auto writer = impl::SimpleByteWriter<true>{ out_buf };
        auto reader = impl::FuncPixelReader{ in_func, channels };

        const auto count = impl::encode(writer, reader, width, height, channels, colorspace);
        if (not count) {
            return make_error<usize>(Error::NotEnoughSpace);
        }
        return count.value();
    }

    Result<usize> encode_into(ByteSinkFun out_func, ByteCSpan in_data, Desc desc)
    {
        const auto [width, height, channels, colorspace] = desc;

        if (in_data.size() == 0) {
            return make_error<usize>(Error::Empty);
        } else if (not desc_is_valid(desc)) {
            return make_error<usize>(Error::InvalidDesc);
        } else if (const auto bytes_count = count_bytes(desc); not bytes_count) {
            return make_error<usize>(Error::TooBig);
        } else if (in_data.size() != bytes_count) {
            return make_error<usize>(Error::MismatchedDesc);
        }

        auto writer = impl::FuncByteWriter{ out_func };
        auto reader = impl::SimplePixelReader{ in_data, channels };

        // TODO: allow function interruption and handle interruption
        return impl::encode(writer, reader, width, height, channels, colorspace).value();
    }

    Result<usize> encode_into(ByteSinkFun out_func, PixelGenFun in_func, Desc desc)
    {
        const auto [width, height, channels, colorspace] = desc;
        if (not desc_is_valid(desc)) {
            return make_error<usize>(Error::InvalidDesc);
        } else if (const auto bytes_count = count_bytes(desc); not bytes_count) {
            return make_error<usize>(Error::TooBig);
        }

        auto writer = impl::FuncByteWriter{ out_func };
        auto reader = impl::FuncPixelReader{ in_func, channels };

        // TODO: allow function and handle interruption
        return impl::encode(writer, reader, width, height, channels, colorspace).value();
    }

    Result<usize> encode_into(const fs::path& out_path, ByteCSpan in_data, Desc desc, bool overwrite) noexcept
    {
        if (fs::exists(out_path) and not overwrite) {
            return make_error<usize>(Error::FileExists);
        } else if (fs::exists(out_path) and not fs::is_regular_file(out_path)) {
            return make_error<usize>(Error::NotRegularFile);
        } else if (const auto bytes_count = count_bytes(desc); not bytes_count) {
            return make_error<usize>(Error::TooBig);
        }

        auto encoded = encode(in_data, desc);
        if (not encoded) {
            return make_error<usize>(encoded.error());
        }

        auto file = std::ofstream{ out_path, std::ios::binary | std::ios::trunc };
        if (not file.is_open()) {
            return make_error<usize>(Error::IoError);
        }

        const auto size = static_cast<std::streamsize>(encoded->size());
        file.write(reinterpret_cast<const char*>(encoded->data()), size);
        if (not file) {
            return make_error<usize>(Error::IoError);
        }

        return encoded->size();
    }

    Result<usize> encode_into(
        const fs::path& out_path,
        PixelGenFun     in_func,
        Desc            desc,
        bool            overwrite
    ) noexcept
    {
        if (fs::exists(out_path) and not overwrite) {
            return make_error<usize>(Error::FileExists);
        } else if (fs::exists(out_path) and not fs::is_regular_file(out_path)) {
            return make_error<usize>(Error::NotRegularFile);
        } else if (const auto bytes_count = count_bytes(desc); not bytes_count) {
            return make_error<usize>(Error::TooBig);
        }

        auto encoded = encode(in_func, desc);
        if (not encoded) {
            return make_error<usize>(encoded.error());
        }

        auto file = std::ofstream{ out_path, std::ios::binary | std::ios::trunc };
        if (not file.is_open()) {
            return make_error<usize>(Error::IoError);
        }

        const auto size = static_cast<std::streamsize>(encoded->size());
        file.write(reinterpret_cast<const char*>(encoded->data()), size);
        if (not file) {
            return make_error<usize>(Error::IoError);
        }

        return encoded->size();
    }

    Result<Image> decode(ByteCSpan in_data, std::optional<Channels> target, bool flip_vertically) noexcept
    {
        if (in_data.size() == 0) {
            return make_error<Image>(Error::Empty);
        } else if (in_data.size() <= constants::header_size + constants::end_marker.size()) {
            return make_error<Image>(Error::TooShort);
        }

        auto header = read_header(in_data);
        if (not header.has_value()) {
            return make_error<Image>(header.error());
        }

        auto& [width, height, channels, colorspace] = header.value();

        const auto src  = channels;
        const auto dest = target.value_or(channels);

        channels = dest;

        const auto bytes_count = count_bytes(header.value());
        if (not bytes_count) {
            return make_error<Image>(Error::TooBig);
        }

        auto result = ByteVec{};
        try {
            result = ByteVec(*bytes_count);
        } catch (...) {
            return make_error<Image>(Error::BadAlloc);
        }

        auto writer = impl::SimplePixelWriter<false>{ result, dest };

        impl::decode(writer, in_data, src, width, height);

        if (flip_vertically) {
            const auto linesize = width * static_cast<usize>(dest);
            for (usize y = 0; y < height / 2; ++y) {
                auto* line1 = result.data() + y * linesize;
                auto* line2 = result.data() + (height - y - 1) * linesize;
                std::swap_ranges(line1, line1 + linesize, line2);
            }
        }

        return Image{
            .data = std::move(result),
            .desc = std::move(header).value(),
        };
    }

    Result<Image> decode(
        const fs::path&         in_path,
        std::optional<Channels> target,
        bool                    flip_vertically
    ) noexcept
    {
        if (not fs::exists(in_path)) {
            return make_error<Image>(Error::FileNotExists);
        } else if (not fs::is_regular_file(in_path)) {
            return make_error<Image>(Error::NotRegularFile);
        }

        auto file = std::ifstream{ in_path, std::ios::binary };
        if (not file.is_open()) {
            return make_error<Image>(Error::IoError);
        }

        auto sstream = std::stringstream{};
        sstream << file.rdbuf();
        if (not file) {
            return make_error<Image>(Error::IoError);
        }

        auto view = sstream.view();
        auto span = ByteCSpan{ reinterpret_cast<const unsigned char*>(view.data()), view.size() };
        return decode(span, target, flip_vertically);
    }

    Result<Desc> decode_into(
        ByteSpan                out_buf,
        ByteCSpan               in_data,
        std::optional<Channels> target,
        bool                    flip_vertically
    )
    {
        if (in_data.size() == 0) {
            return make_error<Desc>(Error::Empty);
        } else if (in_data.size() <= constants::header_size + constants::end_marker.size()) {
            return make_error<Desc>(Error::TooShort);
        }

        auto header = read_header(in_data);
        if (not header.has_value()) {
            return make_error<Desc>(header.error());
        }

        auto& [width, height, channels, colorspace] = header.value();

        const auto src  = channels;
        const auto dest = target.value_or(channels);

        if (const auto bytes_count = count_bytes(header.value()); not bytes_count) {
            return make_error<Desc>(Error::TooBig);
        } else if (out_buf.size() < bytes_count) {
            return make_error<Desc>(Error::NotEnoughSpace);
        }

        channels = dest;

        auto writer = impl::SimplePixelWriter<true>{ out_buf, dest };

        impl::decode(writer, in_data, src, width, height);

        if (flip_vertically) {
            const auto linesize = width * static_cast<usize>(dest);
            for (usize y = 0; y < height / 2; ++y) {
                auto* line1 = out_buf.data() + y * linesize;
                auto* line2 = out_buf.data() + (height - y - 1) * linesize;
                std::swap_ranges(line1, line1 + linesize, line2);
            }
        }

        return std::move(header).value();
    }

    Result<Desc> decode_into(PixelSinkFun out_func, ByteCSpan in_data)
    {
        if (in_data.size() == 0) {
            return make_error<Desc>(Error::Empty);
        } else if (in_data.size() <= constants::header_size + constants::end_marker.size()) {
            return make_error<Desc>(Error::TooShort);
        }

        auto header = read_header(in_data);
        if (not header.has_value()) {
            return make_error<Desc>(header.error());
        }

        auto& [width, height, channels, colorspace] = header.value();

        auto writer = impl::FuncPixelWriter{ out_func };
        impl::decode(writer, in_data, channels, width, height);

        return std::move(header).value();
    }

    Result<Desc> decode_into(
        ByteSpan                out_buf,
        const fs::path&         in_path,
        std::optional<Channels> target,
        bool                    flip_vertically
    ) noexcept
    {
        if (not fs::exists(in_path)) {
            return make_error<Desc>(Error::FileNotExists);
        } else if (not fs::is_regular_file(in_path)) {
            return make_error<Desc>(Error::NotRegularFile);
        }

        auto file = std::ifstream{ in_path, std::ios::binary };
        if (not file.is_open()) {
            return make_error<Desc>(Error::IoError);
        }

        auto sstream = std::stringstream{};
        sstream << file.rdbuf();
        if (not file) {
            return make_error<Desc>(Error::IoError);
        }

        auto view = sstream.view();
        auto span = ByteCSpan{ reinterpret_cast<const unsigned char*>(view.data()), view.size() };
        return decode_into(out_buf, span, target, flip_vertically);
    }

    Result<Desc> decode_into(PixelSinkFun out_func, const fs::path& in_path) noexcept
    {
        if (not fs::exists(in_path)) {
            return make_error<Desc>(Error::FileNotExists);
        } else if (not fs::is_regular_file(in_path)) {
            return make_error<Desc>(Error::NotRegularFile);
        }

        auto file = std::ifstream{ in_path, std::ios::binary };
        if (not file.is_open()) {
            return make_error<Desc>(Error::IoError);
        }

        auto sstream = std::stringstream{};
        sstream << file.rdbuf();
        if (not file) {
            return make_error<Desc>(Error::IoError);
        }

        auto view = sstream.view();
        auto span = ByteCSpan{ reinterpret_cast<const unsigned char*>(view.data()), view.size() };
        return decode_into(out_func, span);
    }
}
