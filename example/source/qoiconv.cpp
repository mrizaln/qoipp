#include "qoipp.hpp"

#include "timer.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

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

#define print(...)   std::cout << std::format(__VA_ARGS__)
#define println(...) print(__VA_ARGS__) << '\n'

namespace fs = std::filesystem;

using qoipp::ByteSpan;
using qoipp::ByteVec;
using qoipp::ImageDesc;
using qoipp::QoiImage;

struct StbImage
{
    using Data       = unsigned char;
    using UniqueData = std::unique_ptr<Data, decltype([](Data* data) { stbi_image_free(data); })>;

    UniqueData m_data;
    ImageDesc  m_desc;
};

struct Image
{
    using Variant = std::variant<StbImage, QoiImage>;

    decltype(auto) visit(auto&& visitor) const
    {
        return std::visit(std::forward<decltype(visitor)>(visitor), m_value);
    }

    Variant m_value;
};

template <typename... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
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

Image readPng(const fs::path& filepath)
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
            .m_width      = width,
            .m_height     = height,
            .m_channels   = channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
            .m_colorSpace = qoipp::ColorSpace::sRGB,    // dummy; unused
        },
    } };
}

Image readQoi(const fs::path& filepath)
{
    auto bytes   = loadFile(filepath);
    auto decoded = DO_TIME_MS ("Decode qoi (qoipp)")
    {
        return qoipp::decode(bytes);
    };
    return { decoded };
}

void writePng(const Image& image, const fs::path& filepath)
{
    using Data        = StbImage::Data;
    const auto tup    = std::make_tuple<Data*, const ImageDesc&>;
    auto [data, desc] = image.visit(Overloaded{
        [&](const StbImage& d) { return tup(d.m_data.get(), d.m_desc); },
        [&](const QoiImage& d) { return tup((Data*)d.m_data.data(), d.m_desc); },
    });

    auto [width, height, channels, _]{ desc };
    auto status = DO_TIME_MS ("Encode png (stb) [and write to file]")
    {
        return stbi_write_png(filepath.c_str(), width, height, (int)channels, data, 0);
    };

    if (status == 0) {
        throw std::runtime_error{ std::format("Failed to write image to '{}'", filepath.c_str()) };
    };
}

void writeQoi(const Image& image, const fs::path& filepath)
{
    const auto tup    = std::make_tuple<ByteSpan, const ImageDesc&>;
    auto [data, desc] = image.visit(Overloaded{
        [&](const StbImage& d) {
            auto [width, height, channels, _]{ d.m_desc };
            const auto size = static_cast<std::size_t>(width * height * (int)channels);
            return tup({ reinterpret_cast<std::byte*>(d.m_data.get()), size }, d.m_desc);
        },
        [&](const QoiImage& d) { return tup(d.m_data, d.m_desc); },
    });

    auto encoded = DO_TIME_MS ("Encode qoi (qoipp)")
    {
        return qoipp::encode(data, desc);
    };
    std::ofstream out{ filepath, std::ios::binary | std::ios::trunc };

    DO_TIME_MS ("Write to file (qoipp)") {
        out.write(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    };
}

void printHelp(const char* prog)
{
    println("Usage   : {} <infile> <outfile>", prog);
    println("Examples: {} input.png output.qoi", prog);
    println("          {} input.qoi output.png", prog);
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
    if (argc <= 1) {
        printHelp(argv[0]);
        return 0;
    } else if (argc <= 2) {
        println("Please provide a path to output file\n");
        printHelp(argv[0]);
        return 1;
    } else if (argc > 3) {
        println("Ignoring extraneous arguments");
    }

    fs::path inputPath{ argv[1] };
    fs::path outputPath{ argv[2] };

    auto [input, output] = validate(inputPath, outputPath);
    if (input == FileType::PNG && output == FileType::QOI) {
        auto image = readPng(inputPath);
        writeQoi(image, outputPath);
    } else if (input == FileType::QOI && output == FileType::PNG) {
        auto image = readQoi(inputPath);
        writePng(image, outputPath);
    } else [[unlikely]] {
        throw std::runtime_error{ "?????? How did you get here?" };
    }

} catch (std::exception& e) {
    println("Exception occurred: {}\n", e.what());
    printHelp(argv[0]);
    return 1;
}
