#include "qoipp.hpp"

#include "timer.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <fmt/core.h>
#include <CLI/CLI.hpp>

#include <stdexcept>
#include <utility>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <tuple>
#include <variant>
#include <format>

namespace fs = std::filesystem;

using qoipp::ByteSpan;
using qoipp::ByteVec;
using qoipp::Image;
using qoipp::ImageDesc;

struct StbImage
{
    using Data       = unsigned char;
    using UniqueData = std::unique_ptr<Data, decltype([](Data* data) { stbi_image_free(data); })>;

    UniqueData m_data;
    ImageDesc  m_desc;
};

struct ImageVar
{
    using Variant = std::variant<StbImage, Image>;

    template <typename... Ts>
    struct Overloaded : Ts...
    {
        using Ts::operator()...;
    };

    std::pair<ByteSpan, ImageDesc> get() const
    {
        const auto tup = std::make_pair<ByteSpan, const ImageDesc&>;
        return std::visit(
            Overloaded{
                [&](const Image& d) { return tup(d.m_data, d.m_desc); },
                [&](const StbImage& d) {
                    auto [width, height, channels, _]{ d.m_desc };
                    const auto size = (width * height * static_cast<std::size_t>(channels));
                    return tup({ reinterpret_cast<std::byte*>(d.m_data.get()), size }, d.m_desc);
                },
            },
            m_value
        );
    }

    void printInfo()
    {
        const auto& [_, desc]                      = get();
        auto [width, height, channels, colorspace] = desc;
        fmt::println("ImageDesc:");
        fmt::println("\twidth     : {}", width);
        fmt::println("\theight    : {}", height);
        fmt::println("\tchannels  : {}", channels == qoipp::Channels::RGB ? "RGB" : "RGBA");
        fmt::println("\tcolorspace: {}", colorspace == qoipp::Colorspace::sRGB ? "sRGB" : "Linear");
    }

    Variant m_value;
};

enum class FileType
{
    PNG,
    QOI
};

ByteVec loadFile(const fs::path& filepath)
{
    std::ifstream file{ filepath, std::ios::binary };
    if (!file) {
        throw std::runtime_error{ std::format("Failed to open file '{}'", filepath.c_str()) };
    }

    const auto size = fs::file_size(filepath);
    ByteVec    bytes(size);

    DO_TIME_MS ("Read from file") {
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    };

    return bytes;
}

ImageVar readPng(const fs::path& filepath)
{
    auto  bytes = loadFile(filepath);
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

    return { StbImage{
        .m_data = StbImage::UniqueData{ data },
        .m_desc = {
            .m_width      = static_cast<unsigned int>(width),
            .m_height     = static_cast<unsigned int>(height),
            .m_channels   = channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
            .m_colorspace = qoipp::Colorspace::sRGB,    // dummy; unused
        },
    } };
}

ImageVar readQoi(const fs::path& filepath, bool rgbOnly)
{
    auto bytes   = loadFile(filepath);
    auto decoded = DO_TIME_MS ("Decode qoi (qoipp)")
    {
        return qoipp::decode(bytes, rgbOnly ? std::optional{ qoipp::Channels::RGB } : std::nullopt);
    };
    return { decoded };
}

void writePng(const ImageVar& image, const fs::path& filepath)
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

void writeQoi(const ImageVar& image, const fs::path& filepath)
{
    auto [data, desc] = image.get();

    auto encoded = DO_TIME_MS ("Encode qoi (qoipp)")
    {
        return qoipp::encode(data, desc);
    };
    std::ofstream out{ filepath, std::ios::binary | std::ios::trunc };

    DO_TIME_MS ("Write to file (qoipp)") {
        out.write(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    };
}

std::pair<FileType, FileType> validate(const fs::path& input, const fs::path& output) noexcept(false)
{
    const auto isPng = [](const fs::path& p) { return p.extension() == ".png"; };
    const auto isQoi = [](const fs::path& p) { return p.extension() == ".qoi"; };

    if (!fs::exists(input)) {
        throw std::runtime_error{ std::format("Input file does not exist '{}'", input.c_str()) };
    }

    if (input == output) {
        throw std::runtime_error{ "Input and output files must be different" };
    }

    if (!isPng(input) && !isQoi(input)) {
        throw std::runtime_error{ std::format("Invalid input file '{}'", input.c_str()) };
    }

    if (!isPng(output) && !isQoi(output)) {
        throw std::runtime_error{ std::format("Invalid output file: '{}'", output.c_str()) };
    }

    if ((isPng(input) && isPng(output)) || (isQoi(input) && isQoi(output))) {
        throw std::runtime_error{ "Input and output files must be of different types" };
    }

    return {
        isPng(input) ? FileType::PNG : FileType::QOI,
        isPng(output) ? FileType::PNG : FileType::QOI,
    };
}

int main(int argc, char** argv)
try {
    CLI::App app{ "QOI to PNG and PNG to QOI converter" };

    fs::path inputPath;
    fs::path outputPath;
    bool     rgbOnly = false;

    app.add_option("infile", inputPath, "Input filepath")->required();
    app.add_option("outfile", outputPath, "Output filepath")->required();
    app.add_flag("--rgb-only", rgbOnly, "Extract rgb only (for QOI image)");

    if (argc <= 1) {
        fmt::print("{}", app.help());
        return 0;
    }

    CLI11_PARSE(app, argc, argv);

    auto [input, output] = validate(inputPath, outputPath);
    if (input == FileType::PNG && output == FileType::QOI) {
        auto image = readPng(inputPath);
        image.printInfo();
        writeQoi(image, outputPath);
    } else if (input == FileType::QOI && output == FileType::PNG) {
        auto image = readQoi(inputPath, rgbOnly);
        image.printInfo();
        writePng(image, outputPath);
    } else [[unlikely]] {
        throw std::runtime_error{ "?????? How did you get here?" };
    }

} catch (std::exception& e) {
    fmt::println("Exception occurred: {}", e.what());
    return 1;
}
