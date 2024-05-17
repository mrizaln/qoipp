#include <qoipp.hpp>

#include <boost/ut.hpp>
#include <fmt/core.h>
#include <fmt/color.h>
#include <range/v3/view.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
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

using Byte = std::byte;

using qoipp::ByteSpan;
using qoipp::ByteVec;

namespace ut = boost::ut;
using namespace ut::literals;
using namespace ut::operators;

ByteVec toBytes(std::initializer_list<u8> data)
{
    ByteVec result(data.size());
    for (auto i : rv::iota(0u, data.size())) {
        result[i] = Byte(data.begin()[i]);
    }
    return result;
}

ByteVec rgbOnly(ByteSpan data)
{
    if (data.size() % 4) {
        throw std::invalid_argument("data size must be a multiple of 4");
    }

    ByteVec result;
    result.reserve(data.size() / 4 * 3);

    for (const auto& chunk : rv::chunk(data, 4)) {
        result.insert(result.end(), chunk.begin(), chunk.begin() + 3);
    }

    return result;
}

// too bad ut doesn't have something like this that can show diff between two spans
std::string compare(ByteSpan lhs, ByteSpan rhs)
{
    std::string result = fmt::format(
        "SHOULD BE EQUALS FOR ALL ELEMENTS:\nlhs size: {} | rhs size: {}\n", lhs.size(), rhs.size()
    );

    for (const auto& [left, right] : rv::zip(lhs, rhs)) {
        auto diff   = (u8)left - (u8)right;
        auto color  = diff == 0 ? fmt::color::green_yellow : fmt::color::orange_red;
        result     += fmt::format(fg(color), "{:#04x} - {:#04x} = {:#04x}\n", left, right, diff);
    }

    result += fmt::format(
        "{} more from {}",
        lhs.size() > rhs.size() ? lhs.size() - rhs.size() : rhs.size() - lhs.size(),
        lhs.size() > rhs.size() ? "lhs" : "rhs"
    );

    return result;
}

ut::suite threeChannelImage = [] {
    constexpr qoipp::ImageDesc desc{
        .m_width      = 29,
        .m_height     = 17,
        .m_channels   = qoipp::Channels::RGB,
        .m_colorspace = qoipp::Colorspace::sRGB,
    };

    // I cropped a random image on my disk to get this data
    const ByteVec rawImage = toBytes({
#include "image_raw_3.txt"
    });

    // The generated qoi data here is generated using ImageMagick's convert command with the input above
    const ByteVec qoiImage = toBytes({
#include "image_qoi_3.txt"
    });

    "3-channel image encode"_test = [&] {
        const auto encoded = qoipp::encode(rawImage, desc);
        ut::expect(ut::that % encoded.size() == qoiImage.size());
        ut::expect(std::memcmp(encoded.data(), qoiImage.data(), qoiImage.size()) == 0_i)
            << compare(qoiImage, encoded);
    };

    "3-channel image decode"_test = [&] {
        const auto [decoded, actualdesc] = qoipp::decode(qoiImage);
        ut::expect(actualdesc == desc);
        ut::expect(ut::that % decoded.size() == rawImage.size());
        ut::expect(std::memcmp(decoded.data(), rawImage.data(), rawImage.size()) == 0_i)
            << compare(rawImage, decoded);
    };

    "3-channel image decode rgbOnly flag enabled"_test = [&] {
        const auto [decoded, actualdesc] = qoipp::decode(qoiImage, true);
        ut::expect(actualdesc == desc);
        ut::expect(ut::that % decoded.size() == rawImage.size());
        ut::expect(std::memcmp(decoded.data(), rawImage.data(), rawImage.size()) == 0_i)
            << compare(rawImage, decoded);
    };
};

ut::suite fourChannelImage = [] {
    constexpr qoipp::ImageDesc desc{
        .m_width      = 24,
        .m_height     = 14,
        .m_channels   = qoipp::Channels::RGBA,
        .m_colorspace = qoipp::Colorspace::sRGB,
    };

    const ByteVec rawImage = toBytes({
#include "image_raw_4.txt"
    });

    const ByteVec qoiImage = toBytes({
#include "image_qoi_4.txt"
    });

    "4-channel image encode"_test = [&] {
        const auto encoded = qoipp::encode(rawImage, desc);
        ut::expect(encoded.size() == qoiImage.size());
        ut::expect(std::memcmp(encoded.data(), qoiImage.data(), qoiImage.size()) == 0_i)
            << compare(qoiImage, encoded);
    };

    "4-channel image decode"_test = [&] {
        const auto [decoded, actualdesc] = qoipp::decode(qoiImage);
        ut::expect(actualdesc == desc);
        ut::expect(ut::that % decoded.size() == rawImage.size());
        ut::expect(std::memcmp(decoded.data(), rawImage.data(), rawImage.size()) == 0_i)
            << compare(rawImage, decoded);
    };

    "4-channel image decode rgbOnly flag enabled"_test = [&] {
        auto rgbImage = rgbOnly(rawImage);
        auto rgbDesc  = qoipp::ImageDesc{
            desc.m_width, desc.m_height, qoipp::Channels::RGB, desc.m_colorspace
        };

        const auto [decoded, actualdesc] = qoipp::decode(qoiImage, true);
        ut::expect(actualdesc == rgbDesc);
        ut::expect(ut::that % decoded.size() == rgbImage.size());
        ut::expect(std::memcmp(decoded.data(), rgbImage.data(), rgbImage.size()) == 0_i)
            << compare(rgbImage, decoded);
    };
};

int main()
{
    /* empty */
}
