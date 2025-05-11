#include <qoipp.hpp>
#define QOI_IMPLEMENTATION
#include <qoi.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#include <stb_image.h>

#include <boost/ut.hpp>
#include <dtl_modern/dtl_modern.hpp>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <range/v3/view.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(__cpp_lib_expected)
static_assert(std::same_as<qoipp::Result<int>, std::expected<int, qoipp::Error>>);
#endif

namespace rv = ranges::views;

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

using qoipp::Span;
using qoipp::Vec;

namespace fs = std::filesystem;
namespace ut = boost::ut;

using namespace ut::literals;
using namespace ut::operators;

using qoipp::Image;

static inline const fs::path g_test_image_dir = fs::current_path() / "qoi_test_images";
static constexpr auto        g_do_test_images = true;

// ----------------
// helper functions
// ----------------

fs::path mktemp()
{
    auto name = std::tmpnam(nullptr);
    return fs::temp_directory_path() / name;
}

Vec read_file(const fs::path& path)
{
    auto file = std::ifstream{ path, std::ios::binary };
    file.exceptions(std::ios::failbit | std::ios::badbit);
    auto data = Vec(fs::file_size(path));
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return data;
}

Vec rbg_only(Span data)
{
    if (data.size() % 4) {
        throw std::invalid_argument("data size must be a multiple of 4");
    }

    auto result = Vec{};
    result.reserve(data.size() / 4 * 3);

    for (const auto& chunk : rv::chunk(data, 4)) {
        result.insert(result.end(), chunk.begin(), chunk.begin() + 3);
    }

    return result;
}

Image load_image_stb(const fs::path& file)
{
    int   width, height, channels;
    auto* data = stbi_load(file.c_str(), &width, &height, &channels, 0);
    if (data == nullptr) {
        throw std::runtime_error{ fmt::format("Error decoding file '{}' (stb)", file) };
    }

    auto image = Image{
        .data = { data, data + width * height * channels},
        .desc = {
            .width      = static_cast<unsigned int>(width),
            .height     = static_cast<unsigned int>(height),
            .channels   = channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
            .colorspace = qoipp::Colorspace::sRGB,
        },
    };

    stbi_image_free(data);
    return image;
}

Image qoi_encode(const Image& image)
{
    auto desc = qoi_desc{
        .width      = static_cast<unsigned int>(image.desc.width),
        .height     = static_cast<unsigned int>(image.desc.height),
        .channels   = static_cast<unsigned char>(image.desc.channels == qoipp::Channels::RGB ? 3 : 4),
        .colorspace = static_cast<unsigned char>(
            image.desc.colorspace == qoipp::Colorspace::sRGB ? QOI_SRGB : QOI_LINEAR
        ),
    };

    int   len;
    auto* data = qoi_encode(image.data.data(), &desc, &len);
    if (!data) {
        throw std::runtime_error{ "Error encoding image (qoi)" };
    }

    auto* byte_ptr = reinterpret_cast<std::uint8_t*>(data);

    auto result = Image{
        .data = Vec{ byte_ptr, byte_ptr + len },
        .desc = image.desc,
    };

    QOI_FREE(data);
    return result;
}

Image qoi_decode(const Span image)
{
    qoi_desc desc;
    auto*    data = qoi_decode(image.data(), (int)image.size(), &desc, 0);

    if (!data) {
        throw std::runtime_error{ "Error decoding image (qoi)" };
    }

    auto* byte_ptr = reinterpret_cast<std::uint8_t*>(data);
    auto  size     = static_cast<size_t>(desc.width * desc.height * desc.channels);

    auto result = Image{
        .data = { byte_ptr, byte_ptr + size },
        .desc = {
            .width      = desc.width,
            .height     = desc.height,
            .channels   = desc.channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
            .colorspace = desc.colorspace == QOI_SRGB ? qoipp::Colorspace::sRGB : qoipp::Colorspace::Linear,
        },
    };

    QOI_FREE(data);
    return result;
}

std::string compare(Span lhs, Span rhs)
{
    constexpr auto chunk = 32;

    auto to_span = [](auto&& r) { return Span{ r.begin(), r.end() }; };

    auto lhs_chunked = lhs | rv::chunk(chunk) | rv::transform(to_span) | ranges::to<std::vector>();
    auto rhs_chunked = rhs | rv::chunk(chunk) | rv::transform(to_span) | ranges::to<std::vector>();

    auto [lcs, ses, edit_dist] = dtl_modern::diff(lhs_chunked, rhs_chunked, [](Span l, Span r) {
        auto lz = l.size();
        auto rz = r.size();
        return lz != rz ? false : std::equal(l.begin(), l.end(), r.begin(), r.end());
    });

    auto buffer = std::string{};
    auto out    = std::back_inserter(buffer);

    const auto red   = fg(fmt::color::orange_red);
    const auto green = fg(fmt::color::green_yellow);

    auto prev_common = false;

    for (auto [elem, info] : ses.get()) {
        using E = dtl_modern::SesEdit;
        switch (info.m_type) {
        case E::Common: {
            if (not prev_common) {
                prev_common = true;
                fmt::format_to(out, "...\n");
            }
        } break;
        case E::Delete: {
            auto offset = elem.begin() - lhs.begin();
            prev_common = false;
            auto joined = fmt::join(elem, " ");
            fmt::format_to(out, red, "{:>5}-{:>5}: {:02x}\n", offset, offset + chunk, joined);
        } break;
        case E::Add: {
            auto offset = elem.begin() - rhs.begin();
            prev_common = false;
            auto joined = fmt::join(elem, " ");
            fmt::format_to(out, green, "{:>5}-{:>5}: {:02x}\n", offset, offset + chunk, joined);
        } break;
        }
    }

    return buffer;
}

// ----------------------
// tests starts from here
// ----------------------

ut::suite three_channel_image = [] {
    constexpr qoipp::Desc desc{
        .width      = 29,
        .height     = 17,
        .channels   = qoipp::Channels::RGB,
        .colorspace = qoipp::Colorspace::sRGB,
    };

    const Vec raw_image = {
#include "image_raw_3.txt"
    };

    const Vec qoi_image = {
#include "image_qoi_3.txt"
    };

    const Vec qoi_image_incomplete = {
#include "image_qoi_3_incomplete.txt"
    };

    "3-channel image encode"_test = [&] {
        const auto encoded = qoipp::encode(raw_image, desc).value();
        ut::expect(ut::that % encoded.size() == qoi_image.size());
        ut::expect(std::memcmp(encoded.data(), qoi_image.data(), qoi_image.size()) == 0)
            << compare(qoi_image, encoded);
    };

    "3-channel image decode"_test = [&] {
        const auto [decoded, actualdesc] = qoipp::decode(qoi_image).value();
        ut::expect(actualdesc == desc);
        ut::expect(ut::that % decoded.size() == raw_image.size());
        ut::expect(std::memcmp(decoded.data(), raw_image.data(), raw_image.size()) == 0)
            << compare(raw_image, decoded);
    };

    "3-channel image decode wants RGB only"_test = [&] {
        const auto [decoded, actualdesc] = qoipp::decode(qoi_image, qoipp::Channels::RGB).value();
        ut::expect(actualdesc == desc);
        ut::expect(ut::that % decoded.size() == raw_image.size());
        ut::expect(std::memcmp(decoded.data(), raw_image.data(), raw_image.size()) == 0)
            << compare(raw_image, decoded);
    };

    "3-channel image encode to and decode from file"_test = [&] {
        const auto qoifile = mktemp();

        ut::expect(ut::nothrow([&] {
            const auto res = qoipp::encode_to_file(qoifile, raw_image, desc, false);
            ut::expect(res.has_value());
        }));
        ut::expect(ut::nothrow([&] {
            const auto res = qoipp::encode_to_file(qoifile, raw_image, desc, false);
            ut::expect(not res.has_value() and res.error() == qoipp::Error::FileExists);
        }));

        qoipp::Image decoded;
        ut::expect(ut::nothrow([&] { decoded = qoipp::decode_from_file(qoifile).value(); }));
        ut::expect(decoded.desc == desc);
        ut::expect(ut::that % decoded.data.size() == raw_image.size());
        ut::expect(std::memcmp(decoded.data.data(), raw_image.data(), raw_image.size()) == 0)
            << compare(raw_image, decoded.data);

        std::ofstream ofs{ qoifile, std::ios::trunc };
        ut::expect(fs::is_empty(qoifile));

        ut::expect(ut::nothrow([&] {
            const auto res = qoipp::decode_from_file(qoifile);
            ut::expect(not res.has_value() and res.error() == qoipp::Error::Empty);
        })) << "Empty file should error with qoipp::Error::Empty";

        fs::remove(qoifile);

        ut::expect(ut::nothrow([&] {
            const auto res = qoipp::decode_from_file(qoifile);
            ut::expect(not res.has_value() and res.error() == qoipp::Error::FileNotExists);
        })) << "Non-existent file should error with qoipp::Error::FileNotExists";

        ut::expect(!fs::exists(qoifile)) << "File should not be created if it previously not exist";
    };

    "3-channel image header read"_test = [&] {
        const auto header = qoipp::read_header(qoi_image);
        ut::expect(header.has_value()) << "Invalid header";
        ut::expect(*header == desc);

        const auto empty_header = qoipp::read_header(Vec{});
        ut::expect(!empty_header.has_value());

        const auto invalid_header_str = Vec{ 0x00, 0x01, 0x02, 0x03 };
        const auto invalid_header     = qoipp::read_header(invalid_header_str);
        ut::expect(!invalid_header.has_value());
    };

    "3-channel image header read from file"_test = [&] {
        const auto qoifile = mktemp();
        ut::expect(ut::nothrow([&] { qoipp::encode_to_file(qoifile, raw_image, desc, false); }));

        const auto header = qoipp::read_header(qoifile);
        ut::expect(header.has_value()) << "Invalid header";
        ut::expect(*header == desc);

        std::ofstream ofs{ qoifile, std::ios::trunc };
        ut::expect(fs::is_empty(qoifile));

        const auto empty_header = qoipp::read_header(qoifile);
        ut::expect(!empty_header.has_value());

        fs::remove(qoifile);
    };

    "3-channel image decode on incomplete data should still work"_test = [&] {
        const auto [decoded, actualdesc] = qoipp::decode(qoi_image_incomplete).value();
        ut::expect(actualdesc == desc);
        ut::expect(ut::that % decoded.size() == raw_image.size());
    };
};

ut::suite four_channel_image = [] {
    constexpr qoipp::Desc desc{
        .width      = 24,
        .height     = 14,
        .channels   = qoipp::Channels::RGBA,
        .colorspace = qoipp::Colorspace::sRGB,
    };

    const Vec raw_image = {
#include "image_raw_4.txt"
    };

    const Vec qoi_image = {
#include "image_qoi_4.txt"
    };

    const Vec qoi_image_incomplete = {
#include "image_qoi_4_incomplete.txt"
    };

    "4-channel image encode"_test = [&] {
        const auto encoded = qoipp::encode(raw_image, desc).value();
        ut::expect(ut::that % encoded.size() == qoi_image.size());
        ut::expect(std::memcmp(encoded.data(), qoi_image.data(), qoi_image.size()) == 0)
            << compare(qoi_image, encoded);
    };

    "4-channel image decode"_test = [&] {
        const auto [decoded, actualdesc] = qoipp::decode(qoi_image).value();
        ut::expect(actualdesc == desc);
        ut::expect(ut::that % decoded.size() == raw_image.size());
        ut::expect(std::memcmp(decoded.data(), raw_image.data(), raw_image.size()) == 0)
            << compare(raw_image, decoded);
    };

    "4-channel image decode wants RGB only"_test = [&] {
        auto rgb_image = rbg_only(raw_image);
        auto rgb_desc  = qoipp::Desc{ desc.width, desc.height, qoipp::Channels::RGB, desc.colorspace };

        const auto [decoded, actualdesc] = qoipp::decode(qoi_image, qoipp::Channels::RGB).value();
        ut::expect(actualdesc == rgb_desc);
        ut::expect(ut::that % decoded.size() == rgb_image.size());
        ut::expect(std::memcmp(decoded.data(), rgb_image.data(), rgb_image.size()) == 0)
            << compare(rgb_image, decoded);
    };

    "4-channel image encode to and decode from file"_test = [&] {
        auto qoifile = mktemp();

        ut::expect(ut::nothrow([&] {
            const auto res = qoipp::encode_to_file(qoifile, raw_image, desc, false);
            ut::expect(res.has_value());
        }));
        ut::expect(ut::nothrow([&] {
            const auto res = qoipp::encode_to_file(qoifile, raw_image, desc, false);
            ut::expect(not res.has_value() and res.error() == qoipp::Error::FileExists);
        }));

        qoipp::Image decoded;
        ut::expect(ut::nothrow([&] { decoded = qoipp::decode_from_file(qoifile).value(); }));
        ut::expect(decoded.desc == desc);
        ut::expect(ut::that % decoded.data.size() == raw_image.size());
        ut::expect(std::memcmp(decoded.data.data(), raw_image.data(), raw_image.size()) == 0)
            << compare(raw_image, decoded.data);

        std::ofstream ofs{ qoifile, std::ios::trunc };
        ut::expect(fs::is_empty(qoifile));

        ut::expect(ut::nothrow([&] {
            const auto res = qoipp::decode_from_file(qoifile);
            ut::expect(not res.has_value() and res.error() == qoipp::Error::Empty);
        })) << "Empty file should error with qoipp::Error::Empty";

        fs::remove(qoifile);

        ut::expect(ut::nothrow([&] {
            const auto res = qoipp::decode_from_file(qoifile);
            ut::expect(not res.has_value() and res.error() == qoipp::Error::FileNotExists);
        })) << "Non-existent file should error with qoipp::Error::FileNotExists";

        ut::expect(!fs::exists(qoifile)) << "File should not be created if it previously not exist";
    };

    "4-channel image header read"_test = [&] {
        const auto header = qoipp::read_header(qoi_image);
        ut::expect(header.has_value()) << "Invalid header";
        ut::expect(*header == desc);

        const auto empty_header = qoipp::read_header(Vec{});
        ut::expect(!empty_header.has_value());

        const auto invalid_header_str = Vec{ 0x00, 0x01, 0x02, 0x03 };
        const auto invalid_header     = qoipp::read_header(invalid_header_str);
        ut::expect(!invalid_header.has_value());
    };

    "4-channel image header read from file"_test = [&] {
        const auto qoifile = mktemp();
        ut::expect(ut::nothrow([&] { qoipp::encode_to_file(qoifile, raw_image, desc, false); }));

        const auto header = qoipp::read_header(qoifile);
        ut::expect(header.has_value()) << "Invalid header";
        ut::expect(*header == desc);

        std::ofstream ofs{ qoifile, std::ios::trunc };
        ut::expect(fs::is_empty(qoifile));

        const auto empty_header = qoipp::read_header(qoifile);
        ut::expect(!empty_header.has_value());

        fs::remove(qoifile);
    };

    "4-channel image decode on incomplete data should still work"_test = [&] {
        const auto [decoded, actualdesc] = qoipp::decode(qoi_image_incomplete).value();
        ut::expect(actualdesc == desc);
        ut::expect(ut::that % decoded.size() == raw_image.size());
    };
};

ut::suite testing_on_real_images = [] {
    if (not g_do_test_images) {
        return;
    }

    fmt::print("Testing on real images...\n");

    if (!fs::exists(g_test_image_dir)) {
        fmt::println("Test image directory '{}' does not exist, skipping test", g_test_image_dir);
        fmt::println("Use the `fetch_test_images.sh` script to download the test images");
        return;
    }

    for (auto entry : fs::directory_iterator{ g_test_image_dir }) {
        if (entry.path().extension() == ".png") {
            fmt::print("Testing encode on '{}'\n", entry.path().filename());

            "png images round trip test compared to reference"_test = [&] {
                const auto image       = load_image_stb(entry.path());
                const auto qoi_image   = qoi_encode(image);
                const auto qoipp_image = qoipp::encode(image.data, image.desc).value();

                ut::expect(ut::that % qoi_image.data.size() == qoipp_image.size());
                ut::expect(qoi_image.desc == image.desc);
                ut::expect(std::memcmp(qoi_image.data.data(), qoipp_image.data(), qoipp_image.size()) == 0)
                    << compare(qoi_image.data, qoipp_image);
            };
        }
        if (entry.path().extension() == ".qoi") {
            fmt::print("Testing decode on '{}'\n", entry.path().filename());

            "qoipp decode compared to reference"_test = [&] {
                auto qoi_image = read_file(entry.path());

                const auto [raw_image_ref, desc_ref] = qoi_decode(qoi_image);
                const auto [raw_image, desc]         = qoipp::decode_from_file(entry.path()).value();

                ut::expect(ut::that % raw_image_ref.size() == raw_image.size());
                ut::expect(desc_ref == desc);
                ut::expect(std::memcmp(raw_image_ref.data(), raw_image.data(), raw_image.size()) == 0)
                    << compare(raw_image_ref, raw_image);
            };
        }
    }
};

int main()
{
    /* empty */
}
