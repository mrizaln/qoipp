#include <qoipp.hpp>

#include <CLI/App.hpp>
#include <CLI/CLI.hpp>
#include <filesystem>
#include <fmt/core.h>

#include <chrono>
#include <fstream>

int main(int argc, char** argv)
{
    auto app = CLI::App{ "swap qoi channels around :p" };

    auto input = std::filesystem::path{};
    app.add_option("input", input, "the qoi image to be swapped around")->required();

    if (argc <= 1) {
        fmt::print("{}", app.help());
        return 0;
    }

    CLI11_PARSE(app, argc, argv);

    if (not std::filesystem::exists(input)) {
        fmt::println(stderr, "File does not exist");
        return 1;
    } else if (not std::filesystem::is_regular_file(input)) {
        fmt::println(stderr, "File is not a regular file");
        return 1;
    }

    auto maybeHeader = qoipp::readHeaderFromFile(input);
    if (not maybeHeader) {
        fmt::println(stderr, "File is not a qoi image");
        return 1;
    }

    // You can do many things by doing this, like flipping, rotating, or event an entire pre-processing
    // pipeline for the image (single pass only though).
    struct SwapChannels
    {
        qoipp::Image& m_image;

        qoipp::PixelRepr operator()(std::size_t index) const
        {
            auto& [data, desc] = m_image;
            auto idx           = index * static_cast<std::size_t>(desc.m_channels);

            auto r = data[idx + 0];
            auto g = data[idx + 1];
            auto b = data[idx + 2];
            auto a = desc.m_channels == qoipp::Channels::RGBA ? data[idx + 3] : std::byte{ 0xFF };

            return { g, b, r, a };
        }
    };

    auto image    = qoipp::decodeFromFile(input);
    auto now      = std::chrono::steady_clock::now();
    auto swapped  = qoipp::encodeFromFunction(SwapChannels{ image }, image.m_desc);
    auto duration = std::chrono::steady_clock::now() - now;

    auto out = std::fstream{ input, std::ios::out | std::ios::binary };
    out.write(reinterpret_cast<char*>(swapped.data()), static_cast<std::streamsize>(swapped.size()));

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    fmt::println("Swapped channels in {}ms", ms);

    return 0;
}
