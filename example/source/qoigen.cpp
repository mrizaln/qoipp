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

namespace rv = ranges::views;
namespace fs = std::filesystem;

using qoipp::Channels;
using qoipp::Vec;

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
    siv::PerlinNoise noise;
    float            freq;
    int              octave;
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
            info = random_perlin_info();
        }

        fmt::println("\nImageGen initialized with current settings:");
        for (const auto& [i, info] : rv::enumerate(m_perlinInfo)) {
            fmt::print("PerlinInfo #{}:\n", i);
            fmt::print("  Frequency: {}\n", info.freq);
            fmt::print("  Octave   : {}\n", info.octave);
        }
        fmt::println("");
    }

    Vec generate(unsigned int width, unsigned int height)
    {
        const auto pixel_size = width * height;
        const auto channels   = static_cast<std::size_t>(m_channels);

        auto result = Vec{};
        result.reserve(pixel_size * channels);

        auto x_bias = std::vector<float>(channels);
        auto y_bias = std::vector<float>(channels);

        for (auto&& [x, y] : rv::zip(x_bias, y_bias)) {
            x = random<float>(-1.0f, 1.0f);
            y = random<float>(-1.0f, 1.0f);
        };

        for (auto i : rv::iota(0u, pixel_size)) {
            const auto x = i % width;
            const auto y = i / height;

            auto fx = std::vector<float>(channels);
            auto fy = std::vector<float>(channels);

            for (auto&& [fx, fy, info] : rv::zip(fx, fy, m_perlinInfo)) {
                fx = static_cast<float>(x) * info.freq / static_cast<float>(width);
                fy = static_cast<float>(y) * info.freq / static_cast<float>(height);
            }

            auto color = std::vector<std::uint8_t>(channels);

            for (const auto& [j, info] : rv::enumerate(m_perlinInfo)) {
                color[j] = static_cast<std::uint8_t>(
                    info.noise.octave2D_01(fx[j] + x_bias[j], fy[j] + y_bias[j], info.octave) * 0xFF
                );
            }

            result.insert(result.end(), color.begin(), color.end());
        }

        return result;
    }

private:
    Channels                m_channels;
    std::vector<PerlinInfo> m_perlinInfo;

    static PerlinInfo random_perlin_info()
    {
        return {
            .noise  = siv::PerlinNoise{ siv::PerlinNoise::seed_type(std::time(nullptr)) },
            .freq   = random(s_freqRange.first, s_freqRange.second),
            .octave = random(s_octaveRange.first, s_octaveRange.second),
        };
    }
};

int main(int argc, char* argv[])
{
    auto app = CLI::App{ "QOI image file generator" };

    auto outpath  = fs::path{ "out.qoi" };
    auto channels = Channels::RGB;

    unsigned int width  = 500;
    unsigned int height = 500;

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

    auto desc = qoipp::Desc{
        .width      = width,
        .height     = height,
        .channels   = channels,
        .colorspace = qoipp::Colorspace::sRGB,
    };

    auto img_gen = ImageGen{ channels };

    auto bytes = DO_TIME_MS ("Generate image")
    {
        fmt::println("Generating image...");
        return img_gen.generate(desc.width, desc.height);
    };

    auto encoded = DO_TIME_MS ("Encode image")
    {
        fmt::println("Encoding image...");
        return qoipp::encode(bytes, desc);
    };

    auto out = std::ofstream{ outpath, std::ios::binary };
    out.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
}
