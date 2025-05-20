#include "qoipp/simple.hpp"

#include "util.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <utility>

namespace qoipp::impl
{
    using RunningArray = std::array<Pixel, constants::running_array_size>;

    // TODO: maybe add the unchecked template parameter here instead of on SimpleByteWriter
    template <bool Checked, util::PixelReader In, util::ByteWriter Out>
    EncodeStatus encode(
        Out        out,
        In         in,
        u32        width,
        u32        height,
        Channels   channels,
        Colorspace colorspace
    ) noexcept
    {
        auto chunks      = util::ChunkArray<Out, Checked>{ out };    // the encoded data goes here
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

                const u8 index = util::hash(curr_pixel) % constants::running_array_size;

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

                    if (util::should_diff(dr, dg, db)) {
                        chunks.write_diff(dr, dg, db);
                    } else if (util::should_luma(dg, dr_dg, db_dg)) {
                        chunks.write_luma(dg, dr_dg, db_dg);
                    } else {
                        chunks.write_rgb(curr_pixel);
                    }
                }
            }

            prev_pixel = curr_pixel;
            if constexpr (Checked) {
                if (not chunks.ok()) {
                    return { chunks.count(), false };
                }
            }
        }

        if (run > 0) {
            chunks.write_run(run);
            run = 0;
        }
        chunks.write_end_marker();

        return { chunks.count(), chunks.ok() };
    }

    template <util::PixelWriter Out>
    void decode(Out out, ByteCSpan in, Channels channels, usize width, usize height) noexcept(false)
    {
        auto seen_pixels = RunningArray{};
        auto prev_pixel  = constants::start;

        const auto get = [&](usize index) -> u8 { return index < in.size() ? in[index] : 0x00; };

        seen_pixels[util::hash(prev_pixel) % constants::running_array_size] = prev_pixel;

        const auto chunks_size = in.size() - constants::header_size - constants::end_marker.size();
        for (usize pixel_index = 0, data_index = constants::header_size;
             data_index < chunks_size or pixel_index < width * height;
             ++pixel_index) {

            const auto tag        = get(data_index++);
            auto       curr_pixel = prev_pixel;

            switch (tag) {
            case util::Tag::OP_RGB: {
                curr_pixel.r = get(data_index++);
                curr_pixel.g = get(data_index++);
                curr_pixel.b = get(data_index++);
                if (channels == Channels::RGBA) {
                    curr_pixel.a = prev_pixel.a;
                }
            } break;
            case util::Tag::OP_RGBA: {
                curr_pixel.r = get(data_index++);
                curr_pixel.g = get(data_index++);
                curr_pixel.b = get(data_index++);
                if (channels == Channels::RGBA) {
                    curr_pixel.a = get(data_index++);
                }
            } break;
            default:
                switch (tag & 0b11000000) {
                case util::Tag::OP_INDEX: {
                    auto& pixel = seen_pixels[tag & 0b00111111];
                    curr_pixel  = pixel;
                } break;
                case util::Tag::OP_DIFF: {
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
                case util::Tag::OP_LUMA: {
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
                case util::Tag::OP_RUN: {
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
            prev_pixel = seen_pixels[util::hash(curr_pixel) % constants::running_array_size] = curr_pixel;
        }
    }
}

namespace qoipp
{
    namespace fs = std::filesystem;

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

        if (not std::ranges::equal(magic, constants::magic)) {
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
            .width      = util::to_native_endian(width),
            .height     = util::to_native_endian(height),
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

        auto data = ByteArr<constants::header_size>{};
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
        } else if (const auto bytes_count = count_bytes(desc); not bytes_count) {
            return make_error<ByteVec>(bytes_count.error());
        } else if (in_data.size() != bytes_count.value()) {
            return make_error<ByteVec>(Error::MismatchedDesc);
        }

        auto result = ByteVec{};
        try {
            result = ByteVec(worst_size(desc).value());
        } catch (...) {
            return make_error<ByteVec>(Error::BadAlloc);
        }

        auto writer = util::SimpleByteWriter{ result };
        auto reader = util::SimplePixelReader{ in_data, channels };

        const auto status = impl::encode<false>(writer, reader, width, height, channels, colorspace);
        assert(status.complete);

        result.resize(status.written);
        return result;
    }

    Result<ByteVec> encode(PixelGenFun in_func, Desc desc) noexcept
    {
        const auto [width, height, channels, colorspace] = desc;
        if (const auto bytes_count = count_bytes(desc); not bytes_count) {
            return make_error<ByteVec>(bytes_count.error());
        }

        auto result = ByteVec{};
        try {
            result = ByteVec(worst_size(desc).value());
        } catch (...) {
            return make_error<ByteVec>(Error::BadAlloc);
        }

        auto writer = util::SimpleByteWriter{ result };
        auto reader = util::FuncPixelReader{ in_func, channels };

        const auto status = impl::encode<false>(writer, reader, width, height, channels, colorspace);
        assert(status.complete);

        result.resize(status.written);
        return result;
    }

    Result<EncodeStatus> encode_into(ByteSpan out_buf, ByteCSpan in_data, Desc desc)
    {
        const auto [width, height, channels, colorspace] = desc;

        if (in_data.size() == 0) {
            return make_error<EncodeStatus>(Error::Empty);
        }

        const auto bytes_count = count_bytes(desc);
        if (not bytes_count) {
            return make_error<EncodeStatus>(bytes_count.error());
        } else if (in_data.size() != bytes_count.value()) {
            return make_error<EncodeStatus>(Error::MismatchedDesc);
        }

        auto writer = util::SimpleByteWriter{ out_buf };
        auto reader = util::SimplePixelReader{ in_data, channels };

        return out_buf.size() >= worst_size(desc).value()
                 ? impl::encode<false>(writer, reader, width, height, channels, colorspace)
                 : impl::encode<true>(writer, reader, width, height, channels, colorspace);
    }

    Result<EncodeStatus> encode_into(ByteSpan out_buf, PixelGenFun in_func, Desc desc)
    {
        const auto [width, height, channels, colorspace] = desc;

        const auto bytes_count = count_bytes(desc);
        if (not bytes_count) {
            return make_error<EncodeStatus>(bytes_count.error());
        }

        auto writer = util::SimpleByteWriter{ out_buf };
        auto reader = util::FuncPixelReader{ in_func, channels };

        return out_buf.size() >= worst_size(desc).value()
                 ? impl::encode<false>(writer, reader, width, height, channels, colorspace)
                 : impl::encode<true>(writer, reader, width, height, channels, colorspace);
    }

    Result<usize> encode_into(ByteSinkFun out_func, ByteCSpan in_data, Desc desc)
    {
        const auto [width, height, channels, colorspace] = desc;

        if (in_data.size() == 0) {
            return make_error<usize>(Error::Empty);
        } else if (const auto bytes_count = count_bytes(desc); not bytes_count) {
            return make_error<usize>(bytes_count.error());
        } else if (in_data.size() != bytes_count.value()) {
            return make_error<usize>(Error::MismatchedDesc);
        }

        auto writer = util::FuncByteWriter{ out_func };
        auto reader = util::SimplePixelReader{ in_data, channels };

        return impl::encode<false>(writer, reader, width, height, channels, colorspace).written;
    }

    Result<usize> encode_into(ByteSinkFun out_func, PixelGenFun in_func, Desc desc)
    {
        const auto [width, height, channels, colorspace] = desc;
        if (const auto bytes_count = count_bytes(desc); not bytes_count) {
            return make_error<usize>(bytes_count.error());
        }

        auto writer = util::FuncByteWriter{ out_func };
        auto reader = util::FuncPixelReader{ in_func, channels };

        return impl::encode<false>(writer, reader, width, height, channels, colorspace).written;
    }

    Result<usize> encode_into(const fs::path& out_path, ByteCSpan in_data, Desc desc, bool overwrite) noexcept
    {
        if (fs::exists(out_path) and not overwrite) {
            return make_error<usize>(Error::FileExists);
        } else if (fs::exists(out_path) and not fs::is_regular_file(out_path)) {
            return make_error<usize>(Error::NotRegularFile);
        } else if (const auto bytes_count = count_bytes(desc); not bytes_count) {
            return make_error<usize>(bytes_count.error());
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
            return make_error<usize>(bytes_count.error());
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
            return make_error<Image>(bytes_count.error());
        }

        auto result = ByteVec{};
        try {
            result = ByteVec(*bytes_count);
        } catch (...) {
            return make_error<Image>(Error::BadAlloc);
        }

        auto writer = util::SimplePixelWriter<false>{ result, dest };

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

        const auto bytes_count = count_bytes(header.value());
        if (not bytes_count) {
            return make_error<Desc>(bytes_count.error());
        } else if (out_buf.size() < bytes_count.value()) {
            return make_error<Desc>(Error::NotEnoughSpace);
        }

        channels = dest;

        if (out_buf.size() >= *bytes_count) {
            auto writer = util::SimplePixelWriter<false>{ out_buf, dest };
            impl::decode(writer, in_data, src, width, height);
        } else {
            auto writer = util::SimplePixelWriter<true>{ out_buf, dest };
            impl::decode(writer, in_data, src, width, height);
        }

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

        auto writer = util::FuncPixelWriter{ out_func };
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
