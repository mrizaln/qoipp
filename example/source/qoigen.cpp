#include "timer.hpp"

#include <CLI/CLI.hpp>
#include <PerlinNoise.hpp>
#include <fmt/core.h>
#include <qoipp.hpp>
#include <range/v3/algorithm.hpp>
#include <range/v3/view.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <type_traits>

namespace rr = ranges::views;
namespace rv = ranges::views;
namespace fs = std::filesystem;

using qoipp::ByteVec;
using qoipp::Channels;

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
    siv::PerlinNoise m_noise;
    float            m_freq;
    int              m_octave;
};

const std::map<std::string, Channels> CHANNELS_STR{
    { "RGB", Channels::RGB },
    { "RGBA", Channels::RGBA },
};

class ImageGen
{
public:
    static constexpr Pair<float> s_freqRange   = { 0.1, 10.0f };
    static constexpr Pair<int>   s_octaveRange = { 1, 4 };

    ImageGen(Channels channels)
        : m_channels{ channels }
        , m_perlinInfo(static_cast<std::size_t>(channels))
    {
        for (auto& info : m_perlinInfo) {
            info = randomPerlinInfo();
        }

        fmt::println("\nImageGen initialized with current settings:");
        for (const auto& [i, info] : rv::enumerate(m_perlinInfo)) {
            fmt::print("PerlinInfo #{}:\n", i);
            fmt::print("  Frequency: {}\n", info.m_freq);
            fmt::print("  Octave   : {}\n", info.m_octave);
        }
        fmt::println("");
    }

    ByteVec generate(unsigned int width, unsigned int height)
    {

        const auto pixelSize = width * height;
        const auto channels  = static_cast<std::size_t>(m_channels);

        ByteVec result;
        result.reserve(pixelSize * channels);

        std::vector<float> xBias(channels);
        std::vector<float> yBias(channels);

        for (auto&& [x, y] : rr::zip(xBias, yBias)) {
            x = random<float>(-1.0f, 1.0f);
            y = random<float>(-1.0f, 1.0f);
        };

        for (auto i : rv::iota(0u, pixelSize)) {
            const auto x = i % width;
            const auto y = i / height;

            std::vector<float> fx(channels);
            std::vector<float> fy(channels);

            for (auto&& [fx, fy, info] : rv::zip(fx, fy, m_perlinInfo)) {
                fx = static_cast<float>(x) * info.m_freq / static_cast<float>(width);
                fy = static_cast<float>(y) * info.m_freq / static_cast<float>(height);
            }

            std::vector<std::byte> color(channels);

            for (const auto& [j, info] : rv::enumerate(m_perlinInfo)) {
                color[j] = static_cast<std::byte>(static_cast<unsigned char>(
                    info.m_noise.octave2D_01(fx[j] + xBias[j], fy[j] + yBias[j], info.m_octave) * 0xFF
                ));
            }

            result.insert(result.end(), color.begin(), color.end());
        }

        return result;
    }

private:
    Channels                m_channels;
    std::vector<PerlinInfo> m_perlinInfo;

    static PerlinInfo randomPerlinInfo()
    {
        return {
            .m_noise  = siv::PerlinNoise{ siv::PerlinNoise::seed_type(std::time(nullptr)) },
            .m_freq   = random(s_freqRange.first, s_freqRange.second),
            .m_octave = random(s_octaveRange.first, s_octaveRange.second),
        };
    }
};

int main(int argc, char* argv[])
{
    CLI::App app{ "QOI image file generator" };

    fs::path     outpath  = "out.qoi";
    unsigned int width    = 500;
    unsigned int height   = 500;
    Channels     channels = Channels::RGB;

    app.add_option("outfile", outpath, "The output filepath for the generated image")->default_val(outpath);
    app.add_option("-w,--width", width, "The width of the qoi image")->required();
    app.add_option("-H,--height", height, "The height of the qoi image")->required();
    app.add_option("-c,--channels", channels, "The channels of the qoi image")
        ->required()
        ->transform(CLI::CheckedTransformer(CHANNELS_STR, CLI::ignore_case));

    if (argc <= 1) {
        fmt::print("{}", app.help());
        return 0;
    }

    CLI11_PARSE(app, argc, argv);

    qoipp::ImageDesc desc{
        .m_width      = width,
        .m_height     = height,
        .m_channels   = channels,
        .m_colorspace = qoipp::Colorspace::sRGB,
    };

    ImageGen imgGen{ channels };

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

    std::ofstream out{ outpath, std::ios::binary };
    out.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
}
