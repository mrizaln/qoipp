#include <qoipp.hpp>

#include <boost/ut.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <cstdint>

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

template <usize N>
using ByteArr = std::array<Byte, N>;
using ByteVec = std::vector<Byte>;

namespace ut = boost::ut;
using namespace ut::literals;
using namespace ut::operators;

void threeChannelImage()
{
    constexpr qoipp::ImageDesc desc{
        .m_width      = 29,
        .m_height     = 17,
        .m_channels   = 3,
        .m_colorSpace = qoipp::ColorSpace::sRGB,
    };

    // I cropped a random image on my disk to get this data
    const std::vector<u8> image = {
#include "image_raw_3.txt"
    };

    // The generated qoi data here is generated using ImageMagick's convert command with the input above
    const std::vector<u8> expected = {
#include "image_qoi_3.txt"
    };

    "3-channel image encode test"_test = [&] {
        const auto actual = qoipp::encode({ (std::byte*)image.data(), image.size() }, desc);
        ut::expect(actual.size() == expected.size());
        ut::expect(std::memcmp(actual.data(), expected.data(), expected.size()) == 0_i);
    };

    "3-channel image decode test"_test = [&] {
        const auto [actual, actualdesc] = qoipp::decode({ (std::byte*)expected.data(), expected.size() });
        ut::expect(actualdesc == desc);
        ut::expect(std::memcmp(actual.data(), image.data(), image.size()) == 0_i);
    };
}

void fourChannelImage()
{
    constexpr qoipp::ImageDesc desc{
        .m_width      = 24,
        .m_height     = 14,
        .m_channels   = 4,
        .m_colorSpace = qoipp::ColorSpace::sRGB,
    };

    const std::vector<u8> image = {
#include "image_raw_4.txt"
    };

    const std::vector<u8> expected = {
#include "image_qoi_4.txt"
    };

    "4-channel image encode test"_test = [&] {
        const auto actual = qoipp::encode({ (std::byte*)image.data(), image.size() }, desc);
        ut::expect(actual.size() == expected.size());
        ut::expect(std::memcmp(actual.data(), expected.data(), expected.size()) == 0_i);
    };

    "4-channel image decode test"_test = [&] {
        const auto [actual, actualdesc] = qoipp::decode({ (std::byte*)expected.data(), expected.size() });
        ut::expect(actualdesc == desc);
        ut::expect(std::memcmp(actual.data(), image.data(), image.size()) == 0_i);
    };
}

int main()
{
    threeChannelImage();
    fourChannelImage();
}
