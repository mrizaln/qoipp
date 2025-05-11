#include <qoipp.hpp>

#include <CLI/App.hpp>
#include <CLI/CLI.hpp>
#include <filesystem>
#include <fmt/core.h>
#include <fmt/std.h>

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

    auto maybeHeader = qoipp::read_header(input);
    if (not maybeHeader) {
        fmt::println(stderr, "File is not a qoi image");
        return 1;
    }

    // You can do many things by doing this, like flipping, rotating, or event an entire pre-processing
    // pipeline for the image (single pass only though).
    struct SwapChannels
    {
        qoipp::Image& image;

        qoipp::Pixel operator()(std::size_t index) const
        {
            auto& [data, desc] = image;
            auto idx           = index * static_cast<std::size_t>(desc.channels);

            return {
                .r = data[idx + 1],
                .g = data[idx + 2],
                .b = data[idx + 0],
                .a = desc.channels == qoipp::Channels::RGBA ? data[idx + 3] : std::uint8_t{ 0xFF },
            };
        }
    };

    auto image = qoipp::decode_from_file(input);
    if (not image) {
        fmt::println(stderr, "failed to decode qoi file {}: {}", input, to_string(image.error()));
        return 1;
    }

    auto now     = std::chrono::steady_clock::now();
    auto swapped = qoipp::encode(SwapChannels{ *image }, image->desc);
    if (not swapped) {
        fmt::println(stderr, "failed to encode into qoi image: {}", to_string(swapped.error()));
    }

    auto duration = std::chrono::steady_clock::now() - now;

    auto out = std::fstream{ input, std::ios::out | std::ios::binary };
    out.write(reinterpret_cast<char*>(swapped->data()), static_cast<std::streamsize>(swapped->size()));

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    fmt::println("Swapped channels in {}ms", ms);

    return 0;
}
