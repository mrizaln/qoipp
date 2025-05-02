#include <qoipp.hpp>
#define QOI_IMPLEMENTATION
#include <qoi.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#include <stb_image.h>

#include <boost/ut.hpp>
#include <fmt/core.h>
#include <fmt/std.h>
#include <fmt/color.h>
#include <range/v3/view.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>

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

static inline const fs::path g_testImageDir = fs::current_path() / "qoi_test_images";

// ----------------
// helper functions
// ----------------

fs::path mktemp()
{
    auto name = std::tmpnam(nullptr);
    return fs::temp_directory_path() / name;
}

Vec readFile(const fs::path& path)
{
    std::ifstream file{ path, std::ios::binary };
    file.exceptions(std::ios::failbit | std::ios::badbit);

    Vec data(fs::file_size(path));
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return data;
}

Vec rgbOnly(Span data)
{
    if (data.size() % 4) {
        throw std::invalid_argument("data size must be a multiple of 4");
    }

    Vec result;
    result.reserve(data.size() / 4 * 3);

    for (const auto& chunk : rv::chunk(data, 4)) {
        result.insert(result.end(), chunk.begin(), chunk.begin() + 3);
    }

    return result;
}

Image loadImageStb(const fs::path& file)
{
    int   width, height, channels;
    auto* data = stbi_load(file.c_str(), &width, &height, &channels, 0);
    if (data == nullptr) {
        throw std::runtime_error{ fmt::format("Error decoding file '{}' (stb)", file) };
    }

    auto* bytePtr = reinterpret_cast<std::uint8_t*>(data);
    auto  size    = static_cast<size_t>(width * height * channels);

    Image image{
        .data = { bytePtr, bytePtr + size },
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

Image qoiEncode(const Image& image)
{
    qoi_desc desc{
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

    auto* bytePtr = reinterpret_cast<std::uint8_t*>(data);

    Image result = {
        .data = Vec{ bytePtr, bytePtr + len },
        .desc = image.desc,
    };

    QOI_FREE(data);
    return result;
}

Image qoiDecode(const Span image)
{
    qoi_desc desc;
    auto*    data = qoi_decode(image.data(), (int)image.size(), &desc, 0);

    if (!data) {
        throw std::runtime_error{ "Error decoding image (qoi)" };
    }

    auto* bytePtr = reinterpret_cast<std::uint8_t*>(data);
    auto  size    = static_cast<size_t>(desc.width * desc.height * desc.channels);

    Image result = {
        .data = { bytePtr, bytePtr + size },
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

// too bad ut doesn't have something like this that can show diff between two spans
std::string compare(Span lhs, Span rhs)
{
    std::string result = fmt::format(
        "SHOULD BE EQUALS FOR ALL ELEMENTS:\nlhs size: {} | rhs size: {}\n", lhs.size(), rhs.size()
    );

    int diffCount = 0;
    for (const auto& [i, pair] : rv::zip(lhs, rhs) | rv::enumerate) {
        auto [left, right] = pair;
        auto diff          = (i32)left - (i32)right;
        if (diff != 0) {
            ++diffCount;
            const auto color = fmt::color::orange_red;
            result += fmt::format(fg(color), "{}: {:#04x} - {:#04x} = {:#04x}\n", i, left, right, diff);
        }
    }

    auto sizeDiff = (i32)lhs.size() - (i32)rhs.size();
    auto min      = std::min(lhs.size(), rhs.size());

    result += "Summary\n";
    result += fmt::format("\t- {} bytes difference in size\n", sizeDiff);
    result += fmt::format("\t- {} difference found ({}%)", diffCount, (f32)diffCount * 100.0F / (f32)min);

    return result;
}

// ----------------------
// tests starts from here
// ----------------------

ut::suite threeChannelImage = [] {
    constexpr qoipp::ImageDesc desc{
        .width      = 29,
        .height     = 17,
        .channels   = qoipp::Channels::RGB,
        .colorspace = qoipp::Colorspace::sRGB,
    };

    const Vec rawImage = {
#include "image_raw_3.txt"
    };

    const Vec qoiImage = {
#include "image_qoi_3.txt"
    };

    const Vec qoiImageIncomplete = {
#include "image_qoi_3_incomplete.txt"
    };

    "3-channel image encode"_test = [&] {
        const auto encoded = qoipp::encode(rawImage, desc);
        ut::expect(ut::that % encoded.size() == qoiImage.size());
        ut::expect(std::memcmp(encoded.data(), qoiImage.data(), qoiImage.size()) == 0)
            << compare(qoiImage, encoded);
    };

    "3-channel image decode"_test = [&] {
        const auto [decoded, actualdesc] = qoipp::decode(qoiImage);
        ut::expect(actualdesc == desc);
        ut::expect(ut::that % decoded.size() == rawImage.size());
        ut::expect(std::memcmp(decoded.data(), rawImage.data(), rawImage.size()) == 0)
            << compare(rawImage, decoded);
    };

    "3-channel image decode wants RGB only"_test = [&] {
        const auto [decoded, actualdesc] = qoipp::decode(qoiImage, qoipp::Channels::RGB);
        ut::expect(actualdesc == desc);
        ut::expect(ut::that % decoded.size() == rawImage.size());
        ut::expect(std::memcmp(decoded.data(), rawImage.data(), rawImage.size()) == 0)
            << compare(rawImage, decoded);
    };

    "3-channel image encode to and decode from file"_test = [&] {
        const auto qoifile = mktemp();

        ut::expect(ut::nothrow([&] { qoipp::encodeToFile(qoifile, rawImage, desc, false); }));
        ut::expect(ut::throws([&] { qoipp::encodeToFile(qoifile, rawImage, desc, false); }));    // file exist

        qoipp::Image decoded;
        ut::expect(ut::nothrow([&] { decoded = qoipp::decodeFromFile(qoifile); }));
        ut::expect(decoded.desc == desc);
        ut::expect(ut::that % decoded.data.size() == rawImage.size());
        ut::expect(std::memcmp(decoded.data.data(), rawImage.data(), rawImage.size()) == 0)
            << compare(rawImage, decoded.data);

        std::ofstream ofs{ qoifile, std::ios::trunc };
        ut::expect(fs::is_empty(qoifile));

        ut::expect(ut::throws([&] { qoipp::decodeFromFile(qoifile); })) << "Empty file should throw";
        fs::remove(qoifile);
        ut::expect(ut::throws([&] { qoipp::decodeFromFile(qoifile); })) << "Non-existent file should throw";
        ut::expect(!fs::exists(qoifile)) << "File should not be created if it previously not exist";
    };

    "3-channel image header read"_test = [&] {
        const auto header = qoipp::readHeader(qoiImage);
        ut::expect(header.has_value()) << "Invalid header";
        ut::expect(*header == desc);

        const auto emptyHeader = qoipp::readHeader(Vec{});
        ut::expect(!emptyHeader.has_value());

        const auto invalidHeaderStr = Vec{ 0x00, 0x01, 0x02, 0x03 };
        const auto invalidHeader    = qoipp::readHeader(invalidHeaderStr);
        ut::expect(!invalidHeader.has_value());
    };

    "3-channel image header read from file"_test = [&] {
        const auto qoifile = mktemp();
        ut::expect(ut::nothrow([&] { qoipp::encodeToFile(qoifile, rawImage, desc, false); }));

        const auto header = qoipp::readHeaderFromFile(qoifile);
        ut::expect(header.has_value()) << "Invalid header";
        ut::expect(*header == desc);

        std::ofstream ofs{ qoifile, std::ios::trunc };
        ut::expect(fs::is_empty(qoifile));

        const auto emptyHeader = qoipp::readHeaderFromFile(qoifile);
        ut::expect(!emptyHeader.has_value());

        fs::remove(qoifile);
    };

    "3-channel image decode on incomplete data should still work"_test = [&] {
        const auto [decoded, actualdesc] = qoipp::decode(qoiImageIncomplete);
        ut::expect(actualdesc == desc);
        ut::expect(ut::that % decoded.size() == rawImage.size());
    };
};

ut::suite fourChannelImage = [] {
    constexpr qoipp::ImageDesc desc{
        .width      = 24,
        .height     = 14,
        .channels   = qoipp::Channels::RGBA,
        .colorspace = qoipp::Colorspace::sRGB,
    };

    const Vec rawImage = {
#include "image_raw_4.txt"
    };

    const Vec qoiImage = {
#include "image_qoi_4.txt"
    };

    const Vec qoiImageIncomplete = {
#include "image_qoi_4_incomplete.txt"
    };

    "4-channel image encode"_test = [&] {
        const auto encoded = qoipp::encode(rawImage, desc);
        ut::expect(encoded.size() == qoiImage.size());
        ut::expect(std::memcmp(encoded.data(), qoiImage.data(), qoiImage.size()) == 0)
            << compare(qoiImage, encoded);
    };

    "4-channel image decode"_test = [&] {
        const auto [decoded, actualdesc] = qoipp::decode(qoiImage);
        ut::expect(actualdesc == desc);
        ut::expect(ut::that % decoded.size() == rawImage.size());
        ut::expect(std::memcmp(decoded.data(), rawImage.data(), rawImage.size()) == 0)
            << compare(rawImage, decoded);
    };

    "4-channel image decode wants RGB only"_test = [&] {
        auto rgbImage = rgbOnly(rawImage);
        auto rgbDesc  = qoipp::ImageDesc{ desc.width, desc.height, qoipp::Channels::RGB, desc.colorspace };

        const auto [decoded, actualdesc] = qoipp::decode(qoiImage, qoipp::Channels::RGB);
        ut::expect(actualdesc == rgbDesc);
        ut::expect(ut::that % decoded.size() == rgbImage.size());
        ut::expect(std::memcmp(decoded.data(), rgbImage.data(), rgbImage.size()) == 0)
            << compare(rgbImage, decoded);
    };

    "4-channel image encode to and decode from file"_test = [&] {
        auto qoifile = mktemp();

        ut::expect(ut::nothrow([&] { qoipp::encodeToFile(qoifile, rawImage, desc, false); }));
        ut::expect(ut::throws([&] { qoipp::encodeToFile(qoifile, rawImage, desc, false); }));    // file exist

        qoipp::Image decoded;
        ut::expect(ut::nothrow([&] { decoded = qoipp::decodeFromFile(qoifile); }));
        ut::expect(decoded.desc == desc);
        ut::expect(ut::that % decoded.data.size() == rawImage.size());
        ut::expect(std::memcmp(decoded.data.data(), rawImage.data(), rawImage.size()) == 0)
            << compare(rawImage, decoded.data);

        std::ofstream ofs{ qoifile, std::ios::trunc };
        ut::expect(fs::is_empty(qoifile));

        ut::expect(ut::throws([&] { qoipp::decodeFromFile(qoifile); })) << "Empty file should throw";
        fs::remove(qoifile);
        ut::expect(ut::throws([&] { qoipp::decodeFromFile(qoifile); })) << "Non-existent file should throw";
        ut::expect(!fs::exists(qoifile)) << "File should not be created if it previously not exist";
    };

    "4-channel image header read"_test = [&] {
        const auto header = qoipp::readHeader(qoiImage);
        ut::expect(header.has_value()) << "Invalid header";
        ut::expect(*header == desc);

        const auto emptyHeader = qoipp::readHeader(Vec{});
        ut::expect(!emptyHeader.has_value());

        const auto invalidHeaderStr = Vec{ 0x00, 0x01, 0x02, 0x03 };
        const auto invalidHeader    = qoipp::readHeader(invalidHeaderStr);
        ut::expect(!invalidHeader.has_value());
    };

    "4-channel image header read from file"_test = [&] {
        const auto qoifile = mktemp();
        ut::expect(ut::nothrow([&] { qoipp::encodeToFile(qoifile, rawImage, desc, false); }));

        const auto header = qoipp::readHeaderFromFile(qoifile);
        ut::expect(header.has_value()) << "Invalid header";
        ut::expect(*header == desc);

        std::ofstream ofs{ qoifile, std::ios::trunc };
        ut::expect(fs::is_empty(qoifile));

        const auto emptyHeader = qoipp::readHeaderFromFile(qoifile);
        ut::expect(!emptyHeader.has_value());

        fs::remove(qoifile);
    };

    "4-channel image decode on incomplete data should still work"_test = [&] {
        const auto [decoded, actualdesc] = qoipp::decode(qoiImageIncomplete);
        ut::expect(actualdesc == desc);
        ut::expect(ut::that % decoded.size() == rawImage.size());
    };
};

ut::suite testingOnRealImage = [] {
    if (!fs::exists(g_testImageDir)) {
        fmt::println("Test image directory '{}' does not exist, skipping test", g_testImageDir);
        fmt::println("Use the `fetch_test_images.sh` script to download the test images");
        return;
    }

    for (auto entry : fs::directory_iterator{ g_testImageDir }) {
        if (entry.path().extension() == ".png") {
            "png images round trip test compared to reference"_test = [&] {
                const auto image      = loadImageStb(entry.path());
                const auto qoiImage   = qoiEncode(image);
                const auto qoippImage = qoipp::encode(image.data, image.desc);

                ut::expect(ut::that % qoiImage.data.size() == qoippImage.size());
                ut::expect(qoiImage.desc == image.desc);
                ut::expect(std::memcmp(qoiImage.data.data(), qoippImage.data(), qoippImage.size()) == 0)
                    << compare(qoiImage.data, qoippImage);
            };
        }
        if (entry.path().extension() == ".qoi") {
            "qoipp decode compared to reference"_test = [&] {
                auto qoiImage = readFile(entry.path());

                const auto [rawImageRef, descRef] = qoiDecode(qoiImage);
                const auto [rawImage, desc]       = qoipp::decodeFromFile(entry.path());

                ut::expect(ut::that % rawImageRef.size() == rawImage.size());
                ut::expect(descRef == desc);
                ut::expect(std::memcmp(rawImageRef.data(), rawImage.data(), rawImage.size()) == 0)
                    << compare(rawImageRef, rawImage);
            };
        }
    }
};

int main()
{
    /* empty */
}
