#include "timer.hpp"

#include <PerlinNoise.hpp>
#include <fmt/core.h>
#include <qoipp.hpp>
#include <range/v3/algorithm.hpp>
#include <range/v3/view.hpp>

#include <array>
#include <cstddef>
#include <fstream>
#include <random>
#include <type_traits>

namespace rv = ranges::views;

using qoipp::ByteVec;

template <typename T>
using Pair = std::pair<T, T>;

template <typename T>
    requires std::is_fundamental_v<T>
T random(T min, T max)
{
    thread_local static std::mt19937 mt{ std::random_device{}() };
    if constexpr (std::is_integral_v<T>) {
        return std::uniform_int_distribution<T>{ min, max }(mt);
    } else {
        return std::uniform_real_distribution<T>{ min, max }(mt);
    }
}

struct PerlinInfo
{
    siv::PerlinNoise m_perlin;
    float            m_perlinFreq;
    int              m_perlinOctave;
};

enum class Channels
{
    Mono = 1,
    RGB  = 3,
    RGBA = 4,
};

template <Channels C>
class ImageGen
{
public:
    static constexpr std::size_t s_channels    = static_cast<std::size_t>(C);
    static constexpr Pair<float> s_freqRange   = { 0.1, 10.0f };
    static constexpr Pair<int>   s_octaveRange = { 1, 4 };

    ImageGen()
    {
        for (auto& info : m_perlinInfo) {
            info = randomPerlinInfo();
        }

        fmt::println("\nImageGen initialized with current settings:");
        for (const auto& [i, info] : rv::enumerate(m_perlinInfo)) {
            fmt::print("PerlinInfo #{}:\n", i);
            fmt::print("  Frequency: {}\n", info.m_perlinFreq);
            fmt::print("  Octave   : {}\n", info.m_perlinOctave);
        }
        fmt::println("");
    }

    ByteVec generate(int width, int height)
    {
        const int pixelSize{ width * height };
        ByteVec   result;
        result.reserve(static_cast<std::size_t>(pixelSize));

        Array<float> xBias;
        Array<float> yBias;

        for (auto i : rv::iota(0u, s_channels)) {
            xBias[i] = random<float>(-1.0f, 1.0f);
            yBias[i] = random<float>(-1.0f, 1.0f);
        };

        for (auto i : rv::iota(0, pixelSize)) {
            const auto x = i % width;
            const auto y = i / height;

            Array<float> fx;
            Array<float> fy;

            for (const auto& [j, info] : rv::enumerate(m_perlinInfo)) {
                fx[j] = (float)x * info.m_perlinFreq / (float)width;
                fy[j] = (float)y * info.m_perlinFreq / (float)height;
            }

            Array<std::byte> color;

            for (const auto& [j, info] : rv::enumerate(m_perlinInfo)) {
                color[j] = static_cast<std::byte>(static_cast<unsigned char>(
                    info.m_perlin.octave2D_01(fx[j] + xBias[j], fy[j] + yBias[j], info.m_perlinOctave) * 0xFF
                ));
            }

            result.insert(result.end(), color.begin(), color.end());
        }

        return result;
    }

private:
    template <typename T>
    struct Array : public std::array<T, s_channels>
    {
        constexpr Array()
            : std::array<T, s_channels>{}
        {
        }

        template <std::ranges::sized_range R>
        constexpr Array(R&& r)
            : std::array<T, s_channels>{}
        {
            std::ranges::copy(r, this->begin());
        }
    };

    Array<PerlinInfo> m_perlinInfo;

    static PerlinInfo randomPerlinInfo()
    {
        return {
            .m_perlin       = siv::PerlinNoise{ siv::PerlinNoise::seed_type(std::time(nullptr)) },
            .m_perlinFreq   = random(s_freqRange.first, s_freqRange.second),
            .m_perlinOctave = random(s_octaveRange.first, s_octaveRange.second),
        };
    }
};

int main()
{
    ImageGen<Channels::RGB> imgGen;

    qoipp::ImageDesc desc{
        .m_width      = 500,
        .m_height     = 500,
        .m_channels   = 3,
        .m_colorSpace = qoipp::ColorSpace::sRGB,
    };

    auto bytes = DO_TIME_MS ("Generate image")
    {
        fmt::println("Generating image...");
        return imgGen.generate(desc.m_width, desc.m_height);
    };

    auto encoded = DO_TIME_MS ("Encode image")
    {
        fmt::println("Encoding image...");
        return qoipp::encode(bytes, desc);
    };

    std::ofstream out{ "out.qoi", std::ios::binary };
    out.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
}
