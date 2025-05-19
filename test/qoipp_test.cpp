#include <qoipp/qoipp.hpp>
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

namespace rr = ranges;
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

using qoipp::CSpan;
using qoipp::Vec;

namespace fs = std::filesystem;
namespace ut = boost::ut;

using qoipp::Image;

static inline const fs::path g_test_image_dir = fs::current_path() / "qoi_test_images";
static constexpr auto        g_do_test_images = true;

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

Vec to_rgb(CSpan data)
{
    if (data.size() % 4) {
        throw std::invalid_argument("data size must be a multiple of 4");
    }

    auto result = Vec{};
    result.reserve(data.size() / 4 * 3);

    for (auto chunk : rv::chunk(data, 4)) {
        result.insert(result.end(), chunk.begin(), chunk.begin() + 3);
    }

    return result;
}

Vec to_rgba(CSpan data)
{
    if (data.size() % 3) {
        throw std::invalid_argument("data size must be a multiple of 3");
    }

    auto result = Vec{};
    result.reserve(data.size() / 3 * 4);

    for (auto chunk : rv::chunk(data, 3)) {
        result.insert(result.end(), chunk.begin(), chunk.begin() + 3);
        result.push_back(0xFF);
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

Image qoi_decode(const CSpan image)
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

std::string compare(CSpan lhs, CSpan rhs)
{
    constexpr auto chunk = 32;

    auto to_span = [](auto&& r) { return CSpan{ r.begin(), r.end() }; };

    auto lhs_chunked = lhs | rv::chunk(chunk) | rv::transform(to_span) | ranges::to<std::vector>();
    auto rhs_chunked = rhs | rv::chunk(chunk) | rv::transform(to_span) | ranges::to<std::vector>();

    auto [lcs, ses, edit_dist] = dtl_modern::diff(lhs_chunked, rhs_chunked, [](CSpan l, CSpan r) {
        auto lz = l.size();
        auto rz = r.size();
        return lz != rz ? false : std::equal(l.begin(), l.end(), r.begin(), r.end());
    });

    auto buffer = std::string{ '\n' };
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
            fmt::format_to(out, red, "{:04x}-{:04x}: {:02x}\n", offset, offset + chunk, joined);
        } break;
        case E::Add: {
            auto offset = elem.begin() - rhs.begin();
            prev_common = false;
            auto joined = fmt::join(elem, " ");
            fmt::format_to(out, green, "{:04x}-{:04x}: {:02x}\n", offset, offset + chunk, joined);
        } break;
        }
    }

    return buffer;
}

int main()
{
    using namespace ut::literals;
    using namespace ut::operators;
    using ut::expect, ut::that;

    // rgb
    // ---
    constexpr auto desc_3 = qoipp::Desc{
        .width      = 29,
        .height     = 17,
        .channels   = qoipp::Channels::RGB,
        .colorspace = qoipp::Colorspace::sRGB,
    };

    const auto raw_image_3 = Vec{
#include "image_raw_3.txt"
    };

    const auto qoi_image_3 = Vec{
#include "image_qoi_3.txt"
    };

    const auto qoi_image_incomplete_3 = Vec{
#include "image_qoi_3_incomplete.txt"
    };

    const auto test_case_3 = std::tie(desc_3, raw_image_3, qoi_image_3, qoi_image_incomplete_3);
    // ---

    // rgba
    // ----
    constexpr auto desc_4 = qoipp::Desc{
        .width      = 24,
        .height     = 14,
        .channels   = qoipp::Channels::RGBA,
        .colorspace = qoipp::Colorspace::sRGB,
    };

    const auto raw_image_4 = Vec{
#include "image_raw_4.txt"
    };

    const auto qoi_image_4 = Vec{
#include "image_qoi_4.txt"
    };

    const auto qoi_image_incomplete_4 = Vec{
#include "image_qoi_4_incomplete.txt"
    };

    const auto test_case_4 = std::tie(desc_4, raw_image_4, qoi_image_4, qoi_image_incomplete_4);
    // ----

    const auto simple_cases = std::array{ test_case_3, test_case_4 };

    "simple image encode"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        const auto encoded = qoipp::encode(raw, desc).value();
        expect(that % encoded.size() == qoi.size());
        expect(rr::equal(encoded, qoi));
    } | simple_cases;

    "image encode into buffer from span"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        "with sufficient buffer"_test = [&] {
            auto worst   = desc.width * desc.height * (static_cast<usize>(desc.channels) + 1) + 14 + 8;
            auto buffer  = Vec(worst);
            auto count   = qoipp::encode_into(buffer, raw, desc).value();
            auto encoded = CSpan{ buffer.begin(), count };

            expect(that % encoded.size() == qoi.size());
            expect(rr::equal(encoded, qoi));
        };

        "with insufficient buffer"_test = [&] {
            auto buffer = Vec(1011);
            auto res    = qoipp::encode_into(buffer, raw, desc);

            expect(not res.has_value() and res.error() == qoipp::Error::NotEnoughSpace);
            expect(rr::equal(buffer, rv::take(qoi, 1011))) << "image should partially encoded";
        };
    } | simple_cases;

    "image encode into buffer from function"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto gen = [&](usize index) -> qoipp::Pixel {
            auto offset = index * static_cast<usize>(desc.channels);
            return {
                raw[offset],
                raw[offset + 1],
                raw[offset + 2],
                desc.channels == qoipp::Channels::RGBA ? raw[offset + 3] : static_cast<u8>(0xFF),
            };
        };

        "with sufficient buffer"_test = [&] {
            auto worst   = desc.width * desc.height * (static_cast<usize>(desc.channels) + 1) + 14 + 8;
            auto buffer  = Vec(worst);
            auto count   = qoipp::encode_into(buffer, gen, desc).value();
            auto encoded = CSpan{ buffer.begin(), count };

            expect(that % encoded.size() == qoi.size());
            expect(rr::equal(encoded, qoi));
        };

        "with insufficient buffer"_test = [&] {
            auto buffer = Vec(1011);
            auto res    = qoipp::encode_into(buffer, gen, desc);

            expect(not res.has_value() and res.error() == qoipp::Error::NotEnoughSpace);
            expect(rr::equal(buffer, rv::take(qoi, 1011))) << "image should partially encoded";
        };
    } | simple_cases;

    "image encode into function from span"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto result = qoipp::Vec{};
        auto out    = [&](u8 byte) { result.push_back(byte); };
        auto res    = qoipp::encode_into(out, raw, desc).value();

        expect(that % res == qoi.size());
        expect(that % result.size() == qoi.size());
        expect(rr::equal(result, qoi));
    } | simple_cases;

    "image encode into function from function"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto gen = [&](usize index) -> qoipp::Pixel {
            auto offset = index * static_cast<usize>(desc.channels);
            return {
                raw[offset],
                raw[offset + 1],
                raw[offset + 2],
                desc.channels == qoipp::Channels::RGBA ? raw[offset + 3] : static_cast<u8>(0xFF),
            };
        };

        auto result = qoipp::Vec{};
        auto out    = [&](u8 byte) { result.push_back(byte); };
        auto res    = qoipp::encode_into(out, gen, desc).value();

        expect(that % res == qoi.size());
        expect(that % result.size() == qoi.size());
        expect(rr::equal(result, qoi));
    } | simple_cases;

    "simple image decode"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        const auto [decoded, actualdesc] = qoipp::decode(qoi).value();
        expect(actualdesc == desc);
        expect(that % decoded.size() == raw.size());
        expect(rr::equal(decoded, raw)) << compare(raw, decoded);
    } | simple_cases;

    "simple image decode wants RGB"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto rgb_image = desc.channels == qoipp::Channels::RGB ? raw : to_rgb(raw);
        auto rgb_desc  = qoipp::Desc{ desc.width, desc.height, qoipp::Channels::RGB, desc.colorspace };

        const auto [decoded, actualdesc] = qoipp::decode(qoi, qoipp::Channels::RGB).value();
        expect(actualdesc == rgb_desc);
        expect(that % decoded.size() == rgb_image.size());
        expect(rr::equal(decoded, rgb_image)) << compare(rgb_image, decoded);
    } | simple_cases;

    "simple image decode wants RGBA"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto rgba_image = desc.channels == qoipp::Channels::RGBA ? raw : to_rgba(raw);
        auto rgba_desc  = qoipp::Desc{ desc.width, desc.height, qoipp::Channels::RGBA, desc.colorspace };

        const auto [decoded, actualdesc] = qoipp::decode(qoi, qoipp::Channels::RGBA).value();
        expect(actualdesc == rgba_desc);
        expect(that % decoded.size() == rgba_image.size());
        expect(rr::equal(decoded, rgba_image)) << compare(rgba_image, decoded);
    } | simple_cases;

    "image decode into buffer"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto size       = desc.width * desc.height * static_cast<usize>(desc.channels);
        auto buffer     = Vec(size);
        auto actualdesc = qoipp::decode_into(buffer, qoi).value();
        auto decoded    = CSpan{ buffer.begin(), size };

        expect(actualdesc == desc);
        expect(that % decoded.size() == raw.size());
        expect(rr::equal(decoded, raw)) << compare(raw, decoded);
    } | simple_cases;

    "image decode into function"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto result = qoipp::Vec{};
        auto out    = [&](qoipp::Pixel pixel) {
            auto arr = std::bit_cast<std::array<u8, 4>>(pixel);
            if (desc.channels == qoipp::Channels::RGBA) {
                result.insert(result.end(), arr.begin(), arr.begin() + 4);
            } else {
                result.insert(result.end(), arr.begin(), arr.begin() + 3);
            }
        };
        auto actualdesc = qoipp::decode_into(out, qoi).value();

        expect(actualdesc == desc);
        expect(that % result.size() == raw.size());
        expect(rr::equal(result, raw)) << compare(raw, result);
    } | simple_cases;

    "image encode to and decode from file"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto qoifile = mktemp();

        expect(ut::nothrow([&] {
            const auto res = qoipp::encode_into(qoifile, raw, desc, false);
            expect(res.has_value());
        }));
        expect(ut::nothrow([&] {
            const auto res = qoipp::encode_into(qoifile, raw, desc, false);
            expect(not res.has_value() and res.error() == qoipp::Error::FileExists);
        }));

        qoipp::Image decoded;
        expect(ut::nothrow([&] { decoded = qoipp::decode(qoifile).value(); }));
        expect(decoded.desc == desc);
        expect(that % decoded.data.size() == raw.size());
        expect(rr::equal(decoded.data, raw)) << compare(raw, decoded.data);

        std::ofstream ofs{ qoifile, std::ios::trunc };
        expect(fs::is_empty(qoifile));

        expect(ut::nothrow([&] {
            const auto res = qoipp::decode(qoifile);
            expect(not res.has_value() and res.error() == qoipp::Error::Empty);
        })) << "Empty file should error with qoipp::Error::Empty";

        fs::remove(qoifile);

        expect(ut::nothrow([&] {
            const auto res = qoipp::decode(qoifile);
            expect(not res.has_value() and res.error() == qoipp::Error::FileNotExists);
        })) << "Non-existent file should error with qoipp::Error::FileNotExists";

        expect(!fs::exists(qoifile)) << "File should not be created if it previously not exist";
    } | simple_cases;

    "image header read"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        const auto header = qoipp::read_header(qoi);
        expect(header.has_value()) << "Invalid header";
        expect(*header == desc);

        const auto empty_header = qoipp::read_header(Vec{});
        expect(!empty_header.has_value());

        const auto invalid_header_str = Vec{ 0x00, 0x01, 0x02, 0x03 };
        const auto invalid_header     = qoipp::read_header(invalid_header_str);
        expect(!invalid_header.has_value());
    } | simple_cases;

    "image header read from file"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        const auto qoifile = mktemp();
        expect(ut::nothrow([&] { qoipp::encode_into(qoifile, raw, desc, false); }));

        const auto header = qoipp::read_header(qoifile);
        expect(header.has_value()) << "Invalid header";
        expect(*header == desc);

        std::ofstream ofs{ qoifile, std::ios::trunc };
        expect(fs::is_empty(qoifile));

        const auto empty_header = qoipp::read_header(qoifile);
        expect(!empty_header.has_value());

        fs::remove(qoifile);
    } | simple_cases;

    "image decode on incomplete data should still work"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, qoi_incomplete] = input;

        const auto [decoded, actualdesc] = qoipp::decode(qoi_incomplete).value();
        expect(actualdesc == desc);
        expect(that % decoded.size() == raw.size());
    } | simple_cases;

    // return early if testing on test images is not enabled
    if (not g_do_test_images) {
        return 0;
    }

    fmt::print("Testing on real images...\n");

    if (!fs::exists(g_test_image_dir)) {
        fmt::println("Test image directory '{}' does not exist, skipping test", g_test_image_dir);
        fmt::println("Use the `fetch_test_images.sh` script to download the test images");
        return 0;
    }

    for (auto entry : fs::directory_iterator{ g_test_image_dir }) {
        if (entry.path().extension() == ".png") {
            fmt::print("Testing encode on '{}'\n", entry.path().filename());

            "png images round trip test compared to reference"_test = [&] {
                const auto image       = load_image_stb(entry.path());
                const auto qoi_image   = qoi_encode(image);
                const auto qoipp_image = qoipp::encode(image.data, image.desc).value();

                expect(that % qoi_image.data.size() == qoipp_image.size());
                expect(qoi_image.desc == image.desc);
                expect(rr::equal(qoi_image.data, qoipp_image)) << compare(qoi_image.data, qoipp_image);
            };
        }
        if (entry.path().extension() == ".qoi") {
            fmt::print("Testing decode on '{}'\n", entry.path().filename());

            "qoipp decode compared to reference"_test = [&] {
                auto qoi_image = read_file(entry.path());

                const auto [raw_image_ref, desc_ref] = qoi_decode(qoi_image);
                const auto [raw_image, desc]         = qoipp::decode(entry.path()).value();

                expect(that % raw_image_ref.size() == raw_image.size());
                expect(desc_ref == desc);
                expect(rr::equal(raw_image_ref, raw_image)) << compare(raw_image_ref, raw_image);
            };
        }
    }
}
