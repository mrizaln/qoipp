#include "qoipp.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

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

    template <usize N>
    using Arr = std::array<u8, N>;

    template <typename T, typename... Ts>
    concept AnyOf = (std::same_as<T, Ts> or ...);

    template <typename T, usize N>
    consteval Arr<N> to_bytes(const T (&value)[N])
    {
        auto result = Arr<N>{};
        for (usize i : sv::iota(0u, N)) {
            result[i] = static_cast<u8>(value[i]);
        }
        return result;
    }

    template <typename T>
        requires std::is_fundamental_v<T>
    constexpr T to_big_endian(const T& value) noexcept
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
    constexpr T from_big_endian(const T& value) noexcept
    {
        return to_big_endian(value);
    }

    struct Pixel
    {
        u8 r;
        u8 g;
        u8 b;
        u8 a;

        constexpr auto operator<=>(const Pixel&) const = default;
    };
    static_assert(std::is_trivial_v<Pixel>);

    template <typename T>
    concept PixelReader = requires(const T ct, usize idx) {
        { ct.read(idx) } -> std::same_as<Pixel>;
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

namespace qoipp::data
{
    // clang-format off
    template <typename T, typename R>
    concept DataChunk = std::ranges::range<R> and requires(const T t, R& r, usize& i) {
        { t.write(r, i) } noexcept -> std::same_as<usize>;
    };
    // clang-format on

    template <typename T>
    concept DataChunkVec = DataChunk<T, Vec>;

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

    struct QoiHeader
    {
        u32 width;
        u32 height;
        u8  channels;
        u8  colorspace;

        usize write(Vec& vec, usize index) const noexcept
        {
            index = write_magic(vec, index);
            index = write32(vec, index, width);
            index = write32(vec, index, height);

            vec[index + 0] = channels;
            vec[index + 1] = colorspace;

            return index + 2;
        }
    };
    static_assert(DataChunkVec<QoiHeader>);

    struct EndMarker
    {
        static usize write(Vec& vec, usize index) noexcept
        {
            for (auto byte : constants::end_marker) {
                vec[index++] = byte;
            }
            return index;
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
            u8 r = 0;
            u8 g = 0;
            u8 b = 0;

            usize write(Vec& vec, usize index) const noexcept
            {
                vec[index + 0] = OP_RGB;
                vec[index + 1] = r;
                vec[index + 2] = g;
                vec[index + 3] = b;
                return index + 4;
            }
        };
        static_assert(DataChunkVec<Rgb>);

        struct Rgba
        {
            u8 r = 0;
            u8 g = 0;
            u8 b = 0;
            u8 a = 0;

            usize write(Vec& vec, usize index) const noexcept
            {
                vec[index + 0] = OP_RGBA;
                vec[index + 1] = r;
                vec[index + 2] = g;
                vec[index + 3] = b;
                vec[index + 4] = a;
                return index + 5;
            }
        };
        static_assert(DataChunkVec<Rgba>);

        struct Index
        {
            u32 index = 0;

            usize write(Vec& vec, usize idx) const noexcept
            {
                vec[idx + 0] = static_cast<u8>(OP_INDEX | index);
                return idx + 1;
            }
        };
        static_assert(DataChunkVec<Index>);

        struct Diff
        {
            i8 dr = 0;
            i8 dg = 0;
            i8 db = 0;

            usize write(Vec& vec, usize index) const noexcept
            {
                constexpr auto bias = constants::bias_op_diff;

                auto val       = OP_DIFF | (dr + bias) << 4 | (dg + bias) << 2 | (db + bias);
                vec[index + 0] = static_cast<u8>(val);

                return index + 1;
            }
        };
        static_assert(DataChunkVec<Diff>);

        struct Luma
        {
            i8 dg    = 0;
            i8 dr_dg = 0;
            i8 db_dg = 0;

            usize write(Vec& vec, usize index) const noexcept
            {
                constexpr auto bias_g  = constants::bias_op_luma_g;
                constexpr auto bias_rb = constants::bias_op_luma_rb;

                vec[index + 0] = static_cast<u8>(OP_LUMA | (dg + bias_g));
                vec[index + 1] = static_cast<u8>((dr_dg + bias_rb) << 4 | (db_dg + bias_rb));

                return index + 2;
            }
        };
        static_assert(DataChunkVec<Luma>);

        struct Run
        {
            i8 run = 0;

            usize write(Vec& vec, usize index) const noexcept
            {
                vec[index + 0] = static_cast<u8>(OP_RUN | (run + constants::bias_op_run));
                return index + 1;
            }
        };
        static_assert(DataChunkVec<Run>);

        template <typename T>
        concept Op = AnyOf<T, Rgb, Rgba, Index, Diff, Luma, Run>;

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
    }
}

namespace qoipp::impl
{
    using RunningArray = std::array<Pixel, constants::running_array_size>;

    class DataChunkArray
    {
    public:
        DataChunkArray(usize size)
            : m_bytes(size)
        {
        }

        template <typename T>
            requires(data::op::Op<T> or AnyOf<T, data::QoiHeader, data::EndMarker>)
        void push(T&& t) noexcept
        {
            m_index = t.write(m_bytes, m_index);
        }

        Vec get() noexcept
        {
            m_bytes.resize(m_index);
            return std::exchange(m_bytes, {});
        }

    private:
        Vec   m_bytes;
        usize m_index = 0;
    };

    // TODO: make Channels parameter a runtime value instead
    template <Channels Chan>
    struct PixelWriter
    {
        std::span<u8> dest;

        void operator()(usize index, const Pixel& pixel) noexcept
        {
            const auto data_index = index * static_cast<usize>(Chan);
            if constexpr (Chan == Channels::RGB) {
                std::memcpy(dest.data() + data_index, &pixel, 3);
            } else {
                std::memcpy(dest.data() + data_index, &pixel, 4);
            }
        }
    };

    // TODO: make Channels parameter a runtime value instead
    template <Channels Chan>
    struct SimplePixelReader
    {
        std::span<const u8> data;

        Pixel read(usize index) const noexcept
        {
            const auto data_index = index * static_cast<usize>(Chan);

            if constexpr (Chan == Channels::RGBA) {
                return {
                    .r = data[data_index + 0],
                    .g = data[data_index + 1],
                    .b = data[data_index + 2],
                    .a = data[data_index + 3],
                };
            } else {
                return {
                    .r = data[data_index + 0],
                    .g = data[data_index + 1],
                    .b = data[data_index + 2],
                    .a = 0xFF,
                };
            }
        }
    };
    static_assert(qoipp::PixelReader<SimplePixelReader<Channels::RGB>>);

    // TODO: make Channels parameter a runtime value instead
    template <Channels Chan>
    struct FuncPixelReader
    {
        PixelGenFun func;
        u32         width;

        Pixel read(usize index) const noexcept
        {
            auto repr = func(index);
            if constexpr (Chan == Channels::RGBA) {
                return {
                    .r = repr.r,
                    .g = repr.g,
                    .b = repr.b,
                    .a = repr.a,
                };
            } else {
                return {
                    .r = repr.r,
                    .g = repr.g,
                    .b = repr.b,
                    .a = 0xFF,
                };
            }
        }
    };

    inline usize hash(const Pixel& pixel)
    {
        const auto& [r, g, b, a] = pixel;
        return (r * 3 + g * 5 + b * 7 + a * 11);
    }

    template <Channels Chan, template <Channels> typename Reader>
    Vec encode(Reader<Chan> reader, u32 width, u32 height, bool srgb)
    {
        // worst possible scenario is when no data is compressed + header + end_marker + tag (rgb/rgba)
        const usize max_size = width * height * (static_cast<usize>(Chan) + 1) + constants::header_size
                             + constants::end_marker.size();

        auto chunks      = DataChunkArray{ max_size };    // the encoded data goes here
        auto seen_pixels = RunningArray{};

        // seen_pixels.fill({ 0x00, 0x00, 0x00, 0x00 });

        chunks.push(data::QoiHeader{
            .width      = width,
            .height     = height,
            .channels   = static_cast<u8>(Chan),
            .colorspace = static_cast<u8>(srgb ? 0 : 1),
        });

        auto prev_pixel = constants::start;
        auto curr_pixel = constants::start;
        i32  run        = 0;

        for (const auto pixel_index : sv::iota(0u, static_cast<usize>(width * height))) {
            curr_pixel = reader.read(pixel_index);

            if (prev_pixel == curr_pixel) {
                run++;

                const bool run_limit  = run == constants::run_limit;
                const bool last_pixel = pixel_index == static_cast<usize>(width * height) - 1;
                if (run_limit || last_pixel) {
                    chunks.push(data::op::Run{ .run = static_cast<i8>(run) });
                    run = 0;
                }
            } else {
                if (run > 0) {
                    // ends of OP_RUN
                    chunks.push(data::op::Run{ .run = static_cast<i8>(run) });
                    run = 0;
                }

                const u8 index = hash(curr_pixel) % constants::running_array_size;

                // OP_INDEX
                if (seen_pixels[index] == curr_pixel) {
                    chunks.push(data::op::Index{ .index = index });
                } else {
                    seen_pixels[index] = curr_pixel;

                    if constexpr (Chan == Channels::RGBA) {
                        if (prev_pixel.a != curr_pixel.a) {
                            // OP_RGBA
                            chunks.push(data::op::Rgba{
                                .r = curr_pixel.r,
                                .g = curr_pixel.g,
                                .b = curr_pixel.b,
                                .a = curr_pixel.a,
                            });

                            prev_pixel = curr_pixel;
                            continue;
                        }
                    }

                    // OP_DIFF and OP_LUMA
                    const i8 dr = curr_pixel.r - prev_pixel.r;
                    const i8 dg = curr_pixel.g - prev_pixel.g;
                    const i8 db = curr_pixel.b - prev_pixel.b;

                    const i8 dr_dg = dr - dg;
                    const i8 db_dg = db - dg;

                    if (data::op::should_diff(dr, dg, db)) {
                        chunks.push(data::op::Diff{
                            .dr = dr,
                            .dg = dg,
                            .db = db,
                        });
                    } else if (data::op::should_luma(dg, dr_dg, db_dg)) {
                        chunks.push(data::op::Luma{
                            .dg    = dg,
                            .dr_dg = dr_dg,
                            .db_dg = db_dg,
                        });
                    } else {
                        // OP_RGB
                        chunks.push(data::op::Rgb{
                            .r = curr_pixel.r,
                            .g = curr_pixel.g,
                            .b = curr_pixel.b,
                        });
                    }
                }
            }

            prev_pixel = curr_pixel;
        }

        chunks.push(data::EndMarker{});

        return chunks.get();
    }

    template <Channels Src, Channels Dest = Src>
    Vec decode(std::span<const u8> data, usize width, usize height, bool flip_vertically) noexcept(false)
    {
        auto decoded     = Vec(width * height * static_cast<u8>(Dest));
        auto seen_pixels = RunningArray{};

        // seen_pixels.fill({ 0x00, 0x00, 0x00, 0x00 });

        auto prev_pixel = constants::start;

        const auto get   = [&](usize index) -> u8 { return index < data.size() ? data[index] : 0x00; };
        auto       write = PixelWriter<Dest>{ decoded };

        seen_pixels[hash(prev_pixel) % constants::running_array_size] = prev_pixel;

        const auto chunks_size = data.size() - constants::header_size - constants::end_marker.size();
        for (usize pixel_index = 0, data_index = constants::header_size;
             data_index < chunks_size || pixel_index < width * height;
             ++pixel_index) {

            const auto tag        = get(data_index++);
            auto       curr_pixel = prev_pixel;

            using T = data::op::Tag;
            switch (tag) {
            case T::OP_RGB: {
                curr_pixel.r = get(data_index++);
                curr_pixel.g = get(data_index++);
                curr_pixel.b = get(data_index++);
                if constexpr (Src == Channels::RGBA) {
                    curr_pixel.a = prev_pixel.a;
                }
            } break;
            case T::OP_RGBA: {
                curr_pixel.r = get(data_index++);
                curr_pixel.g = get(data_index++);
                curr_pixel.b = get(data_index++);
                if constexpr (Src == Channels::RGBA) {
                    curr_pixel.a = get(data_index++);
                }
            } break;
            default:
                switch (tag & 0b11000000) {
                case T::OP_INDEX: {
                    auto& pixel = seen_pixels[tag & 0b00111111];
                    curr_pixel  = pixel;
                } break;
                case T::OP_DIFF: {
                    const i8 dr = ((tag & 0b00110000) >> 4) - constants::bias_op_diff;
                    const i8 dg = ((tag & 0b00001100) >> 2) - constants::bias_op_diff;
                    const i8 db = ((tag & 0b00000011)) - constants::bias_op_diff;

                    curr_pixel.r = static_cast<u8>(dr + prev_pixel.r);
                    curr_pixel.g = static_cast<u8>(dg + prev_pixel.g);
                    curr_pixel.b = static_cast<u8>(db + prev_pixel.b);
                    if constexpr (Src == Channels::RGBA) {
                        curr_pixel.a = static_cast<u8>(prev_pixel.a);
                    }
                } break;
                case T::OP_LUMA: {
                    const auto read_blue = get(data_index++);

                    const u8 dg    = (tag & 0b00111111) - constants::bias_op_luma_g;
                    const u8 dr_dg = ((read_blue & 0b11110000) >> 4) - constants::bias_op_luma_rb;
                    const u8 db_dg = (read_blue & 0b00001111) - constants::bias_op_luma_rb;

                    curr_pixel.r = static_cast<u8>(dg + dr_dg + prev_pixel.r);
                    curr_pixel.g = static_cast<u8>(dg + prev_pixel.g);
                    curr_pixel.b = static_cast<u8>(dg + db_dg + prev_pixel.b);
                    if constexpr (Src == Channels::RGBA) {
                        curr_pixel.a = static_cast<u8>(prev_pixel.a);
                    }
                } break;
                case T::OP_RUN: {
                    auto run = (tag & 0b00111111) - constants::bias_op_run;
                    while (run-- > 0 and pixel_index < width * height) {
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
            auto linesize = width * static_cast<usize>(Dest);
            for (auto y : sv::iota(0u, height / 2)) {
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
    std::optional<ImageDesc> read_header(Span data) noexcept
    {
        if (data.size() < constants::header_size) {
            return std::nullopt;
        }

        using Magic = decltype(constants::magic);
        auto* magic = reinterpret_cast<const Magic*>(data.data());

        if (std::memcmp(magic, constants::magic.data(), constants::magic.size()) != 0) {
            return std::nullopt;
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

        return ImageDesc{
            .width      = from_big_endian(width),
            .height     = from_big_endian(height),
            .channels   = static_cast<Channels>(channels),
            .colorspace = static_cast<Colorspace>(colorspace),
        };
    }

    Vec encode(std::span<const u8> data, ImageDesc desc) noexcept(false)
    {
        const auto [width, height, channels, colorspace] = desc;
        const auto max_size = static_cast<usize>(width * height * static_cast<u32>(channels));

        if (width <= 0 || height <= 0) {
            throw std::invalid_argument{ std::format(
                "Invalid image description: w = {}, h = {}, c = {}", width, height, static_cast<i32>(channels)
            ) };
        }

        if (data.size() != max_size) {
            throw std::invalid_argument{ std::format(
                "Data size does not match the image description: expected {} x {} x {} = {}, got {}",
                width,
                height,
                static_cast<i32>(channels),
                max_size,
                data.size()
            ) };
        }

        if (channels == Channels::RGB && data.size() % 3 != 0) {
            throw std::invalid_argument{
                "Data does not align with the number of channels: expected multiple of 3 bytes"
            };
        } else if (channels == Channels::RGBA && data.size() % 4 != 0) {
            throw std::invalid_argument{
                "Data does not align with the number of channels: expected multiple of 4 bytes"
            };
        }

        const auto is_srgb = colorspace == Colorspace::sRGB;
        if (channels == Channels::RGB) {
            auto reader = impl::SimplePixelReader<Channels::RGB>{ data };
            return impl::encode(reader, width, height, is_srgb);
        } else {
            auto reader = impl::SimplePixelReader<Channels::RGBA>{ data };
            return impl::encode(reader, width, height, is_srgb);
        }
    }

    Vec encode_from_function(PixelGenFun func, ImageDesc desc) noexcept(false)
    {
        const auto [width, height, channels, colorspace] = desc;

        if (width <= 0 || height <= 0) {
            throw std::invalid_argument{ std::format(
                "Invalid image description: w = {}, h = {}, c = {}", width, height, static_cast<i32>(channels)
            ) };
        }

        const auto is_srgb = colorspace == Colorspace::sRGB;
        if (channels == Channels::RGB) {
            auto reader = impl::FuncPixelReader<Channels::RGB>{ std::move(func), width };
            return impl::encode(reader, width, height, is_srgb);
        } else {
            auto reader = impl::FuncPixelReader<Channels::RGBA>{ std::move(func), width };
            return impl::encode(reader, width, height, is_srgb);
        }
    }

    Image decode(Span data, std::optional<Channels> target, bool flip_vertically) noexcept(false)
    {
        if (data.size() == 0) {
            throw std::invalid_argument{ "Data is empty" };
        }

        auto desc = [&] {
            if (auto header = read_header(data); header.has_value()) {
                return header.value();
            } else {
                throw std::invalid_argument{ "Invalid header" };
            }
        }();

        const auto& [width, height, channels, colorspace] = desc;

        auto want = target.value_or(channels);

        if (channels == Channels::RGBA and want == Channels::RGBA) {
            return {
                .data = impl::decode<Channels::RGBA, Channels::RGBA>(data, width, height, flip_vertically),
                .desc = desc,
            };
        } else if (channels == Channels::RGB and want == Channels::RGB) {
            return {
                .data = impl::decode<Channels::RGB, Channels::RGB>(data, width, height, flip_vertically),
                .desc = desc,
            };
        } else if (channels == Channels::RGBA and want == Channels::RGB) {
            desc.channels = Channels::RGB;
            return {
                .data = impl::decode<Channels::RGBA, Channels::RGB>(data, width, height, flip_vertically),
                .desc = desc,
            };
        } else {
            desc.channels = Channels::RGBA;
            return {
                .data = impl::decode<Channels::RGB, Channels::RGBA>(data, width, height, flip_vertically),
                .desc = desc,
            };
        }
    }

    std::optional<ImageDesc> read_header_from_file(const std::filesystem::path& path) noexcept
    {
        namespace fs = std::filesystem;

        if (!fs::exists(path) || fs::file_size(path) < constants::header_size || !fs::is_regular_file(path)) {
            return std::nullopt;
        }

        auto file = std::ifstream{ path, std::ios::binary };
        if (!file.is_open()) {
            return std::nullopt;
        }

        auto data = Arr<constants::header_size>{};
        file.read(reinterpret_cast<char*>(data.data()), constants::header_size);

        return read_header(data);
    }

    void encode_to_file(
        const std::filesystem::path& path,
        std::span<const u8>          data,
        ImageDesc                    desc,
        bool                         overwrite
    ) noexcept(false)
    {
        namespace fs = std::filesystem;

        if (fs::exists(path) && !overwrite) {
            throw std::invalid_argument{ "File already exists and overwrite is false" };
        }

        if (fs::exists(path) && !fs::is_regular_file(path)) {
            throw std::invalid_argument{ "Path is not a regular file, cannot overwrite" };
        }

        auto encoded = encode(data, desc);

        auto file = std::ofstream{ path, std::ios::binary | std::ios::trunc };
        if (!file.is_open()) {
            throw std::invalid_argument{ "Could not open file for writing" };
        }

        file.write(
            reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size())
        );
    }

    Image decode_from_file(
        const std::filesystem::path& path,
        std::optional<Channels>      target,
        bool                         flip_vertically
    ) noexcept(false)
    {
        namespace fs = std::filesystem;

        if (!fs::exists(path) || !fs::is_regular_file(path)) {
            throw std::invalid_argument{ "Path does not exist or is not a regular file" };
        }

        auto file = std::ifstream{ path, std::ios::binary };
        if (!file.is_open()) {
            throw std::invalid_argument{ "Could not open file for reading" };
        }

        auto data = Vec(fs::file_size(path));
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));

        return decode(data, target, flip_vertically);
    }
}
