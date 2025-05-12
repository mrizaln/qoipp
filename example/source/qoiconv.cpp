#include "qoipp.hpp"

#include "timer.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <stb_image_write.h>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>

namespace fs = std::filesystem;

using qoipp::CSpan;
using qoipp::Desc;
using qoipp::Image;
using qoipp::Vec;

struct StbImage
{
    using Data       = unsigned char;
    using UniqueData = std::unique_ptr<Data, decltype([](Data* data) { stbi_image_free(data); })>;

    UniqueData data;
    Desc       desc;
};

struct ImageVar
{
    using Variant = std::variant<StbImage, Image>;

    template <typename... Ts>
    struct Overloaded : Ts...
    {
        using Ts::operator()...;
    };

    std::pair<CSpan, Desc> get() const
    {
        const auto tup = std::make_pair<CSpan, const Desc&>;
        return std::visit(
            Overloaded{
                [&](const Image& d) { return tup(d.data, d.desc); },
                [&](const StbImage& d) {
                    auto [width, height, channels, _]{ d.desc };
                    const auto size = (width * height * static_cast<std::size_t>(channels));
                    return tup({ d.data.get(), size }, d.desc);
                },
            },
            value
        );
    }

    void print_info()
    {
        const auto& [_, desc]                      = get();
        auto [width, height, channels, colorspace] = desc;
        fmt::println("Desc:");
        fmt::println("\twidth     : {}", width);
        fmt::println("\theight    : {}", height);
        fmt::println("\tchannels  : {}", channels == qoipp::Channels::RGB ? "RGB" : "RGBA");
        fmt::println("\tcolorspace: {}", colorspace == qoipp::Colorspace::sRGB ? "sRGB" : "Linear");
    }

    Variant value;
};

enum class FileType
{
    PNG,
    QOI
};

Vec load_file(const fs::path& filepath)
{
    auto file = std::ifstream{ filepath, std::ios::binary };
    if (!file) {
        throw std::runtime_error{ std::format("Failed to open file '{}'", filepath.c_str()) };
    }

    const auto size  = fs::file_size(filepath);
    auto       bytes = Vec(size);

    DO_TIME_MS ("Read from file") {
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    };

    return bytes;
}

ImageVar read_png(const fs::path& filepath)
{
    auto  bytes = load_file(filepath);
    int   width, height, channels;
    auto* data = DO_TIME_MS ("Decode png (stb)")
    {
        return stbi_load_from_memory(
            reinterpret_cast<unsigned char*>(bytes.data()),
            static_cast<int>(bytes.size()),
            &width,
            &height,
            &channels,
            0
        );
    };

    if (data == nullptr) {
        throw std::runtime_error{ std::format("Failed to load PNG image '{}'", filepath.c_str()) };
    }

    auto channels_real = qoipp::to_channels(channels);
    if (not channels_real.has_value()) {
        throw std::runtime_error{ std::format("{} number of channels is not supported", channels) };
    }

    return { StbImage{
        .data = StbImage::UniqueData{ data },
        .desc = {
            .width      = static_cast<unsigned int>(width),
            .height     = static_cast<unsigned int>(height),
            .channels   = *channels_real,
            .colorspace = qoipp::Colorspace::sRGB,    // dummy; unused
        },
    } };
}

ImageVar read_qoi(const fs::path& filepath, bool rgb_only)
{
    auto bytes   = load_file(filepath);
    auto decoded = DO_TIME_MS ("Decode qoi (qoipp)")
    {
        return qoipp::decode(bytes, rgb_only ? std::optional{ qoipp::Channels::RGB } : std::nullopt).value();
    };
    return { decoded };
}

void write_png(const ImageVar& image, const fs::path& filepath)
{
    const auto& [data, desc] = image.get();

    auto status = DO_TIME_MS ("Encode png (stb) [and write to file]")
    {
        using Data                        = StbImage::Data;
        auto [width, height, channels, _] = desc;
        return stbi_write_png(
            filepath.c_str(),
            (int)width,
            (int)height,
            (int)channels,
            reinterpret_cast<const Data*>(data.data()),
            0
        );
    };

    if (status == 0) {
        throw std::runtime_error{ std::format("Failed to write image to '{}'", filepath.c_str()) };
    };
}

void write_qoi(const ImageVar& image, const fs::path& filepath)
{
    auto [data, desc] = image.get();

    auto encoded = DO_TIME_MS ("Encode qoi (qoipp)")
    {
        return qoipp::encode(data, desc).value();
    };
    auto out = std::ofstream{ filepath, std::ios::binary | std::ios::trunc };

    DO_TIME_MS ("Write to file (qoipp)") {
        out.write(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    };
}

std::pair<FileType, FileType> validate(const fs::path& input, const fs::path& output) noexcept(false)
{
    const auto is_png = [](const fs::path& p) { return p.extension() == ".png"; };
    const auto is_qoi = [](const fs::path& p) { return p.extension() == ".qoi"; };

    if (!fs::exists(input)) {
        throw std::runtime_error{ std::format("Input file does not exist '{}'", input.c_str()) };
    }

    if (input == output) {
        throw std::runtime_error{ "Input and output files must be different" };
    }

    if (!is_png(input) && !is_qoi(input)) {
        throw std::runtime_error{ std::format("Invalid input file '{}'", input.c_str()) };
    }

    if (!is_png(output) && !is_qoi(output)) {
        throw std::runtime_error{ std::format("Invalid output file: '{}'", output.c_str()) };
    }

    if ((is_png(input) && is_png(output)) || (is_qoi(input) && is_qoi(output))) {
        throw std::runtime_error{ "Input and output files must be of different types" };
    }

    return {
        is_png(input) ? FileType::PNG : FileType::QOI,
        is_png(output) ? FileType::PNG : FileType::QOI,
    };
}

int main(int argc, char** argv)
try {
    auto app = CLI::App{ "QOI to PNG and PNG to QOI converter" };

    auto input_path  = fs::path{};
    auto output_path = fs::path{};
    auto rgb_only    = false;

    app.add_option("infile", input_path, "Input filepath")->required();
    app.add_option("outfile", output_path, "Output filepath")->required();
    app.add_flag("--rgb-only", rgb_only, "Extract rgb only (for QOI image)");

    if (argc <= 1) {
        fmt::print("{}", app.help());
        return 0;
    }

    CLI11_PARSE(app, argc, argv);

    auto [input, output] = validate(input_path, output_path);
    if (input == FileType::PNG && output == FileType::QOI) {
        auto image = read_png(input_path);
        image.print_info();
        write_qoi(image, output_path);
    } else if (input == FileType::QOI && output == FileType::PNG) {
        auto image = read_qoi(input_path, rgb_only);
        image.print_info();
        write_png(image, output_path);
    } else [[unlikely]] {
        throw std::runtime_error{ "?????? How did you get here?" };
    }

} catch (std::exception& e) {
    fmt::println("Exception occurred: {}", e.what());
    return 1;
}
