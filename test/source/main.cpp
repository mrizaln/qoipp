#include "qoipp.hpp"
#include "timer.hpp"

#include <fmt/core.h>
#include <fmt/std.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <cstddef>
#include <tuple>

namespace fs = std::filesystem;

int main(int argc, char** argv)
try {
    if (argc <= 1) {
        fmt::println("usage: {} <png file> <qoi file>", argv[0]);
        return 0;
    }

    auto [input, output] = [&] {
        fs::path infile{ argv[1] };

        if (infile.extension() != ".png") {
            fmt::println("file does not have png extension: {}", infile.c_str());
            std::exit(1);
        } else if (!fs::exists(infile)) {
            fmt::println("files does not exist: {}", infile.c_str());
            std::exit(1);
        }

        if (argc > 2) {
            return std::make_tuple(infile, fs::path{ argv[2] });
        } else {
            auto outfile = infile;
            return std::make_tuple(infile, outfile.replace_extension(".qoi"));
        }
    }();

    fmt::println(">>> input: {}\n>>> output: {}", input, output);

    int   width, height, channels;
    auto* data = DO_TIME_MS ("Loading image (stb_image)")
    {
        auto* data = stbi_load(input.c_str(), &width, &height, &channels, 0);
        fmt::println(">>> image info: {}x{} ({} chan)", width, height, channels);
        return data;
    };

    std::span<std::byte const> span{
        reinterpret_cast<std::byte*>(data),
        static_cast<std::size_t>(width * height * channels),
    };
    qoipp::ImageDesc desc{
        .m_width    = width,
        .m_height   = height,
        .m_channels = channels,
    };

    auto encoded = DO_TIME_MS ("Encoding (qoipp)")
    {
        return qoipp::encode(span, desc);
    };

    stbi_image_free(data);

    DO_TIME_MS ("writing file") {
        std::ofstream ofstream{ output, std::ios::binary | std::ios::trunc };
        ofstream.write(
            reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size())
        );
    };
} catch (std::exception& e) {
    fmt::println("Exception occurred: {}", e.what());
}
