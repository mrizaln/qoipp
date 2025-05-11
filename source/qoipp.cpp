#include "qoipp.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include <utility>

namespace fs = std::filesystem;

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

    template <usize N>
    using Arr = std::array<u8, N>;

    template <typename T, typename... Ts>
    concept AnyOf = (std::same_as<T, Ts> or ...);

    template <typename T, usize N>
    consteval Arr<N> to_bytes(const T (&value)[N])
    {
        auto result = Arr<N>{};
        for (usize i = 0; i < N; ++i) {
            result[i] = static_cast<u8>(value[i]);
        }
        return result;
    }

    template <typename T>
        requires std::is_fundamental_v<T>
    constexpr T to_native_endian(const T& value) noexcept
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

    static_assert(std::is_trivial_v<Pixel>);

    template <class T>
    std::decay_t<T> decay_copy(T&&);

    template <typename T>
    concept PixelReader = requires (const T ct, T t, usize idx) {
        { ct.read(idx) } -> std::same_as<Pixel>;
        { decay_copy(t.channels) } -> std::same_as<Channels>;
    };
}

namespace qoipp::constants
{
    constexpr std::array magic = { 'q', 'o', 'i', 'f' };

    constexpr usize header_size        = 14;
    constexpr usize running_array_size = 64;
    constexpr Arr   end_marker         = to_bytes({ 0, 0, 0, 0, 0, 0, 0, 1 });

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
    inline usize write_magic(Vec& vec, usize index) noexcept
    {
        for (char c : constants::magic) {
            vec[index++] = static_cast<u8>(c);
        }
        return index;
    }

    inline usize write32(Vec& vec, usize index, u32 value) noexcept
    {
        vec[index + 0] = static_cast<u8>((value >> 24) & 0xFF);
        vec[index + 1] = static_cast<u8>((value >> 16) & 0xFF);
        vec[index + 2] = static_cast<u8>((value >> 8) & 0xFF);
        vec[index + 3] = static_cast<u8>(value & 0xFF);
        return index + 4;
    }

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
        return dr >= constants::min_diff && dr <= constants::max_diff    //
            && dg >= constants::min_diff && dg <= constants::max_diff    //
            && db >= constants::min_diff && db <= constants::max_diff;
    }

    inline bool should_luma(i8 dg, i8 dr_dg, i8 db_dg) noexcept
    {
        return dr_dg >= constants::min_luma_rb && dr_dg <= constants::max_luma_rb    //
            && db_dg >= constants::min_luma_rb && db_dg <= constants::max_luma_rb    //
            && dg >= constants::min_luma_g && dg <= constants::max_luma_g;
    }

    class ChunkArray
    {
    public:
        ChunkArray(usize size)
            : m_bytes(size)
        {
        }

        void write_header(u32 width, u32 height, Channels channels, Colorspace colorspace) noexcept
        {
            m_index = op::write_magic(m_bytes, m_index);
            m_index = op::write32(m_bytes, m_index, width);
            m_index = op::write32(m_bytes, m_index, height);

            m_bytes[m_index++] = static_cast<u8>(channels);
            m_bytes[m_index++] = static_cast<u8>(colorspace);
        }

        void write_end_marker() noexcept
        {
            for (auto byte : constants::end_marker) {
                m_bytes[m_index++] = byte;
            }
        }

        void write_rgb(const Pixel& pixel) noexcept
        {
            m_bytes[m_index++] = Tag::OP_RGB;
            std::memcpy(m_bytes.data() + m_index, &pixel, 3);
            m_index += 3;
        }

        void write_rgba(const Pixel& pixel) noexcept
        {
            m_bytes[m_index++] = Tag::OP_RGBA;
            std::memcpy(m_bytes.data() + m_index, &pixel, 4);
            m_index += 4;
        }

        void write_index(u8 index) noexcept
        {
            m_bytes[m_index++] = Tag::OP_INDEX | index;    //
        }

        void write_diff(i8 dr, i8 dg, i8 db) noexcept
        {
            const auto bias    = constants::bias_op_diff;
            const auto val     = Tag::OP_DIFF | (dr + bias) << 4 | (dg + bias) << 2 | (db + bias);
            m_bytes[m_index++] = static_cast<u8>(val);
        }

        void write_luma(i8 dg, i8 dr_dg, i8 db_dg) noexcept
        {
            const auto bias_g  = constants::bias_op_luma_g;
            const auto bias_rb = constants::bias_op_luma_rb;

            m_bytes[m_index++] = static_cast<u8>(Tag::OP_LUMA | (dg + bias_g));
            m_bytes[m_index++] = static_cast<u8>((dr_dg + bias_rb) << 4 | (db_dg + bias_rb));
        }

        void write_run(u8 run) noexcept
        {
            m_bytes[m_index++] = static_cast<u8>(Tag::OP_RUN | (run + constants::bias_op_run));
        }

        Vec get() noexcept
        {
            m_bytes.resize(m_index);
            return std::move(m_bytes);
        }

    private:
        Vec   m_bytes;
        usize m_index = 0;
    };
}

namespace qoipp::util
{
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
};

namespace qoipp::impl
{
    using RunningArray = std::array<Pixel, constants::running_array_size>;

    struct SimplePixelWriter
    {
        std::span<u8> dest;
        Channels      channels;

        void operator()(usize index, const Pixel& pixel) noexcept
        {
            const auto offset = index * static_cast<usize>(channels);
            if (channels == Channels::RGBA) {
                std::memcpy(dest.data() + offset, &pixel, 4);
            } else {
                std::memcpy(dest.data() + offset, &pixel, 3);
            }
        }
    };

    struct SimplePixelReader
    {
        std::span<const u8> data;
        Channels            channels;

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

    inline usize hash(const Pixel& pixel)
    {
        const auto& [r, g, b, a] = pixel;
        return (r * 3 + g * 5 + b * 7 + a * 11);
    }

    template <PixelReader Reader>
    Vec encode(Reader reader, u32 width, u32 height, Channels channels, Colorspace colorspace)
    {
        // worst possible scenario is when no data is compressed + header + end_marker + tag (rgb/rgba)
        const usize max_size = width * height * (static_cast<usize>(channels) + 1)    // OP_RGBA
                             + constants::header_size + constants::end_marker.size();

        auto chunks      = op::ChunkArray{ max_size };    // the encoded data goes here
        auto seen_pixels = RunningArray{};

        chunks.write_header(width, height, channels, colorspace);

        auto prev_pixel = constants::start;
        auto curr_pixel = constants::start;
        u8   run        = 0;

        for (usize pixel_index = 0u; pixel_index < width * height; ++pixel_index) {
            curr_pixel = reader.read(pixel_index);

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

                    if (reader.channels == Channels::RGBA && prev_pixel.a != curr_pixel.a) {
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
        }

        if (run > 0) {
            chunks.write_run(run);
            run = 0;
        }

        chunks.write_end_marker();

        return chunks.get();
    }

    Vec decode(
        std::span<const u8> data,
        Channels            src,
        Channels            dest,
        usize               width,
        usize               height,
        bool                flip_vertically
    ) noexcept(false)
    {
        auto decoded     = Vec(width * height * static_cast<u8>(dest));
        auto seen_pixels = RunningArray{};

        // seen_pixels.fill({ 0x00, 0x00, 0x00, 0x00 });

        auto prev_pixel = constants::start;

        const auto get   = [&](usize index) -> u8 { return index < data.size() ? data[index] : 0x00; };
        auto       write = SimplePixelWriter{ decoded, dest };

        seen_pixels[hash(prev_pixel) % constants::running_array_size] = prev_pixel;

        const auto chunks_size = data.size() - constants::header_size - constants::end_marker.size();
        for (usize pixel_index = 0, data_index = constants::header_size;
             data_index < chunks_size || pixel_index < width * height;
             ++pixel_index) {

            const auto tag        = get(data_index++);
            auto       curr_pixel = prev_pixel;

            switch (tag) {
            case op::Tag::OP_RGB: {
                curr_pixel.r = get(data_index++);
                curr_pixel.g = get(data_index++);
                curr_pixel.b = get(data_index++);
                if (src == Channels::RGBA) {
                    curr_pixel.a = prev_pixel.a;
                }
            } break;
            case op::Tag::OP_RGBA: {
                curr_pixel.r = get(data_index++);
                curr_pixel.g = get(data_index++);
                curr_pixel.b = get(data_index++);
                if (src == Channels::RGBA) {
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
                    if (src == Channels::RGBA) {
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
                    if (src == Channels::RGBA) {
                        curr_pixel.a = static_cast<u8>(prev_pixel.a);
                    }
                } break;
                case op::Tag::OP_RUN: {
                    auto run = (tag & 0b00111111) - constants::bias_op_run;
                    while (run-- > 0 && pixel_index < width * height) {
                        write(pixel_index++, prev_pixel);
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

            write(pixel_index, curr_pixel);
            prev_pixel = seen_pixels[hash(curr_pixel) % constants::running_array_size] = curr_pixel;
        }

        if (flip_vertically) {
            const auto linesize = width * static_cast<usize>(dest);
            for (usize y = 0; y < height / 2; ++y) {
                auto* line1 = decoded.data() + y * linesize;
                auto* line2 = decoded.data() + (height - y - 1) * linesize;

                std::swap_ranges(line1, line1 + linesize, line2);
            }
        }

        return decoded;
    }
}

namespace qoipp
{
    std::string_view to_string(Error err) noexcept
    {
        switch (err) {
        case Error::Empty: return "Data is empty";
        case Error::FileExists: return "File already exists";
        case Error::FileNotExists: return "File does not exist";
        case Error::InvalidDesc: return "Image description is invalid";
        case Error::IoError: return "Unable to do read or write operation";
        case Error::MismatchedDesc: return "Image description does not match the data";
        case Error::NotQoi: return "Not a qoi file";
        case Error::NotRegularFile: return "Not a regular file";
        case Error::TooShort: return "Data is too short";
        default: return "Unknown";
        }
    }

    Result<Desc> read_header(Span data) noexcept
    {
        if (data.size() < constants::header_size) {
            return util::make_error<Desc>(Error::TooShort);
        }

        using Magic = decltype(constants::magic);
        auto* magic = reinterpret_cast<const Magic*>(data.data());

        if (std::memcmp(magic, constants::magic.data(), constants::magic.size()) != 0) {
            return util::make_error<Desc>(Error::NotQoi);
        }

        auto index = constants::magic.size();

        u32 width, height;
        u32 channels, colorspace;

        std::memcpy(&width, data.data() + index, sizeof(u32));
        index += sizeof(u32);
        std::memcpy(&height, data.data() + index, sizeof(u32));
        index += sizeof(u32);

        channels   = data[index++];
        colorspace = data[index++];

        return Desc{
            .width      = to_native_endian(width),
            .height     = to_native_endian(height),
            .channels   = static_cast<Channels>(channels),
            .colorspace = static_cast<Colorspace>(colorspace),
        };
    }

    Result<Vec> encode(std::span<const u8> data, Desc desc) noexcept
    {
        const auto [width, height, channels, colorspace] = desc;
        const auto max_size = static_cast<usize>(width * height * static_cast<u32>(channels));

        if (width <= 0 || height <= 0) {
            return util::make_error<Vec>(Error::InvalidDesc);
        } else if (data.size() != max_size) {
            return util::make_error<Vec>(Error::MismatchedDesc);
        }

        const auto reader = impl::SimplePixelReader{ data, channels };
        return impl::encode(reader, width, height, channels, colorspace);
    }

    Result<Vec> encode_from_function(PixelGenFun func, Desc desc) noexcept
    {
        const auto [width, height, channels, colorspace] = desc;

        if (width <= 0 || height <= 0) {
            return util::make_error<Vec>(Error::InvalidDesc);
        }

        const auto reader = impl::FuncPixelReader{ std::move(func), channels };
        return impl::encode(reader, width, height, channels, colorspace);
    }

    Result<Image> decode(Span data, std::optional<Channels> target, bool flip_vertically) noexcept
    {
        if (data.size() == 0) {
            return util::make_error<Image>(Error::Empty);
        }

        auto header = read_header(data);
        if (!header.has_value()) {
            return util::make_error<Image>(header.error());
        }

        auto& [width, height, channels, colorspace] = header.value();

        const auto src  = channels;
        const auto dest = target.value_or(channels);

        channels = dest;

        return Image{
            .data = impl::decode(data, src, dest, width, height, flip_vertically),
            .desc = std::move(header).value(),
        };
    }

    Result<Desc> read_header_from_file(const fs::path& path) noexcept
    {
        if (!fs::exists(path)) {
            return util::make_error<Desc>(Error::FileNotExists);
        } else if (fs::file_size(path) < constants::header_size) {
            return util::make_error<Desc>(Error::TooShort);
        } else if (!fs::is_regular_file(path)) {
            return util::make_error<Desc>(Error::NotRegularFile);
        }

        auto file = std::ifstream{ path, std::ios::binary };
        if (!file.is_open()) {
            return util::make_error<Desc>(Error::IoError);
        }

        auto data = Arr<constants::header_size>{};
        file.read(reinterpret_cast<char*>(data.data()), constants::header_size);
        if (!file) {
            return util::make_error<Desc>(Error::IoError);
        }

        return read_header(data);
    }

    Result<void> encode_to_file(
        const fs::path&     path,
        std::span<const u8> data,
        Desc                desc,
        bool                overwrite
    ) noexcept
    {
        namespace fs = fs;

        if (fs::exists(path) && !overwrite) {
            return util::make_error<void>(Error::FileExists);
        } else if (fs::exists(path) && !fs::is_regular_file(path)) {
            return util::make_error<void>(Error::NotRegularFile);
        }

        auto encoded = encode(data, desc);
        if (!encoded) {
            return util::make_error<void>(encoded.error());
        }

        auto file = std::ofstream{ path, std::ios::binary | std::ios::trunc };
        if (!file.is_open()) {
            return util::make_error<void>(Error::IoError);
        }

        const auto size = static_cast<std::streamsize>(encoded->size());
        file.write(reinterpret_cast<const char*>(encoded->data()), size);
        if (!file) {
            return util::make_error<void>(Error::IoError);
        }

        return {};
    }

    Result<Image> decode_from_file(
        const fs::path&         path,
        std::optional<Channels> target,
        bool                    flip_vertically
    ) noexcept
    {
        namespace fs = fs;

        if (!fs::exists(path)) {
            return util::make_error<Image>(Error::FileNotExists);
        } else if (!fs::is_regular_file(path)) {
            return util::make_error<Image>(Error::NotRegularFile);
        }

        auto file = std::ifstream{ path, std::ios::binary };
        if (!file.is_open()) {
            return util::make_error<Image>(Error::IoError);
        }

        auto sstream = std::stringstream{};
        sstream << file.rdbuf();
        if (!file) {
            return util::make_error<Image>(Error::IoError);
        }

        auto view = sstream.view();
        auto span = Span{ reinterpret_cast<const unsigned char*>(view.data()), view.size() };
        return decode(span, target, flip_vertically);
    }
}
