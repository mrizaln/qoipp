#include <qoipp.hpp>
#define QOI_IMPLEMENTATION
#include <qoi.h>
#include <qoixx.hpp>
#include <fpng.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <fmt/std.h>
#include <fmt/color.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

namespace fs = std::filesystem;

using Clock     = std::chrono::high_resolution_clock;
using TimePoint = Clock::time_point;
using Duration  = Clock::duration;    // nano seconds

struct FmtFill
{
    const char* value;
    std::size_t width;
};

template <>
struct fmt::formatter<FmtFill>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
    constexpr auto format(FmtFill f, format_context& ctx)
    {
        auto&& out = ctx.out();
        for (std::size_t i = 0; i < f.width; ++i) {
            fmt::format_to(out, "{}", f.value);
        }
        return ctx.out();
    }
};

namespace lib
{
    enum class Lib
    {
        none,
        qoi,
        qoixx,
        qoipp,
        stb,
        fpng,
    };

    std::map<Lib, std::string_view> names = {
        { Lib::none, "none" },   { Lib::qoi, "qoi" }, { Lib::qoixx, "qoixx" },
        { Lib::qoipp, "qoipp" }, { Lib::stb, "stb" }, { Lib::fpng, "fpng" },
    };
};

struct RawImage
{
    qoipp::Vec       data;
    qoipp::ImageDesc desc;
};

struct QoiImage
{
    qoipp::Vec       data;
    qoipp::ImageDesc desc;
};

struct PngImage
{
    qoipp::Vec       data;
    qoipp::ImageDesc desc;
};

template <typename Image = QoiImage>
struct EncodeResult
{
    Image    image;
    Duration time;
};

struct DecodeResult
{
    RawImage image;
    Duration time;
};

auto descString(const qoipp::ImageDesc& desc)
{
    return fmt::format(
        "{}x{} ({}|{})",
        desc.width,
        desc.height,
        fmt::underlying(desc.channels),
        fmt::underlying(desc.colorspace)
    );
}

struct Options
{
    bool         warmup     = true;
    bool         verify     = true;
    bool         reference  = true;
    bool         encode     = true;
    bool         decode     = true;
    bool         recurse    = true;
    bool         onlyTotals = false;
    bool         color      = true;
    bool         stb        = true;
    bool         fpng       = true;
    unsigned int runs       = 1;

    void configure(CLI::App& app)
    {
        app.add_option("runs", runs, "Number of runs")->default_val(runs)->check(CLI::PositiveNumber);

        app.add_flag("!--no-warmup", warmup, "Don't perform a warmup run");
        app.add_flag("!--no-verify", verify, "Don't verify qoi roundtrip");
        app.add_flag("!--no-reference", reference, "Don't run reference implementation");
        app.add_flag("!--no-encode", encode, "Don't run encoders");
        app.add_flag("!--no-decode", decode, "Don't run decoders");
        app.add_flag("!--no-recurse", recurse, "Don't descend into directories");
        app.add_flag("!--no-color", color, "Don't print with color");
        app.add_flag("!--no-stb", stb, "Don't benchmark stb");
        app.add_flag("!--no-fpng", fpng, "Don't benchmark fpng");
        app.add_flag("--only-totals", onlyTotals, "Don't print individual image results");
    }

    void print()
    {
        fmt::println("Options:");
        fmt::println("\t- runs      : {}", runs);
        fmt::println("\t- warmup    : {}", warmup);
        fmt::println("\t- verify    : {}", verify);
        fmt::println("\t- reference : {}", reference);
        fmt::println("\t- encode    : {}", encode);
        fmt::println("\t- decode    : {}", decode);
        fmt::println("\t- recurse   : {}", recurse);
        fmt::println("\t- color     : {}", color);
        fmt::println("\t- stb       : {}", stb);
        fmt::println("\t- fpng      : {}", fpng);
        fmt::println("\t- onlytotals: {}", onlyTotals);
    }
};

struct BenchmarkResult
{
    struct LibInfo
    {
        Duration    encodeTime  = {};
        Duration    decodeTime  = {};
        std::size_t encodedSize = 0;
    };

    qoipp::ImageDesc desc    = {};
    fs::path         file    = {};
    std::size_t      rawSize = 0;

    std::map<lib::Lib, LibInfo> libsInfo = {};

    void print_sep(const char* start, const char* end, const char* mid, const char* fill) const
    {
        auto ff = [f = fill](std::size_t size) { return FmtFill{ f, size }; };
        // clang-format off
        fmt::println(
            "{0}{3}{2}{4}{2}{5}{2}{6}{2}{7}{2}{8}{2}{9}{2}{10}{2}{11}{1}", start, end, mid,
            ff(10), ff(11), ff(11), ff(14), ff(14), ff(8), ff(8), ff(12), ff(10)
        );
        // clang-format on
    };

    void print_header(auto&&... c) const
        requires(sizeof...(c) == 9)
    {
        fmt::println(
            "┃ {:^8} ┃ {:^9} ┃ {:^9} ┃ {:^12} ┃ {:^12} ┃ {:^6} ┃ {:^6} ┃ {:^10} ┃ {:^8} ┃",
            std::forward<decltype(c)>(c)...
        );
    };

    void print_row(auto&&... c) const
        requires(sizeof...(c) == 9)
    {
        //   | name  | enc      | dec      | enc/px    | dec/px    | enc%    | dec%    | size   | ratio      |
        fmt::println(
            "│ {:<8} │ {:>9.3f} │ {:>9.3f} │ {:>12.3f} │ {:>12.3f} │ {:>+5}% │ {:>+5}% │ {:>10} │ {:>6.1f} % "
            "│",
            std::forward<decltype(c)>(c)...
        );
    };

    void print(bool color) const
    {
        using Lib     = lib::Lib;
        using Millis  = std::chrono::duration<float, std::milli>;
        using Micros  = std::chrono::duration<float, std::micro>;
        auto toMillis = std::chrono::duration_cast<Millis, Duration::rep, Duration::period>;
        auto toMicros = std::chrono::duration_cast<Micros, Duration::rep, Duration::period>;

        struct Printed
        {
            float       totalEncodeTime;
            float       totalDecodeTime;
            float       pixelsPerEncode;
            float       pixelsPerDecode;
            std::size_t encodedSizeKiB;
            float       encodeSizeRatio;
        };

        auto pixelCount = desc.width * desc.height;

        std::map<Lib, Printed> printed;
        for (const auto& [lib, info] : libsInfo) {
            printed.emplace(
                lib,
                Printed{
                    .totalEncodeTime = toMillis(info.encodeTime).count(),
                    .totalDecodeTime = toMillis(info.decodeTime).count(),
                    .pixelsPerEncode = (float)pixelCount / toMicros(info.encodeTime).count(),
                    .pixelsPerDecode = (float)pixelCount / toMicros(info.decodeTime).count(),
                    .encodedSizeKiB  = info.encodedSize / 1000,
                    .encodeSizeRatio = (float)info.encodedSize / (float)rawSize,
                }
            );
        }

        const auto [width, height, channels, _] = desc;
        fmt::println(
            "File: '{}' [{} x {} ({})]",
            file,
            width,
            height,
            channels == qoipp::Channels::RGB ? "RGB" : "RGBA"
        );

        if (libsInfo.empty()) {
            fmt::println("\tNo results");
            return;
        }

        // header
        // ------
        // clang-format off
        print_sep("┏", "┓", "┳", "━");
        print_header( "", "enc (ms)", "dec (ms)", "px/enc (/us)", "px/dec (/us)", "enc t+", "dec t+", "size (KiB)", "ratio");
        print_sep("┡", "┩", "╇", "━");
        // clang-format on

        const auto getCodecRatio = [&](const Printed& info) -> std::pair<int, int> {
            const auto percent = [](auto old, auto neww) { return (neww - old) / old * 100; };

            if (printed.contains(lib::Lib::qoi)) {
                auto qoi     = printed[lib::Lib::qoi];
                auto qoiEnc  = qoi.totalEncodeTime;
                auto qoiDec  = qoi.totalDecodeTime;
                auto infoEnc = info.totalEncodeTime;
                auto infoDec = info.totalDecodeTime;

                if (qoiEnc <= std::numeric_limits<float>::epsilon()) {
                    return { 0, percent(qoiDec, infoDec) };
                } else if (qoiDec <= std::numeric_limits<float>::epsilon()) {
                    return { percent(qoiEnc, infoEnc), 0 };
                } else {
                    return { percent(qoiEnc, infoEnc), percent(qoiDec, infoDec) };
                }
            } else {
                return { 0, 0 };
            }
        };

        for (const auto& [lib, info] : printed) {
            auto [encRatio, decRatio] = getCodecRatio(info);
            if (color) {
                print_row(
                    lib::names[lib],
                    info.totalEncodeTime,
                    info.totalDecodeTime,
                    info.pixelsPerEncode,
                    info.pixelsPerDecode,
                    fmt::styled(encRatio, fmt::bg(encRatio > 0 ? fmt::color::orange_red : fmt::color::green)),
                    fmt::styled(decRatio, fmt::bg(decRatio > 0 ? fmt::color::orange_red : fmt::color::green)),
                    info.encodedSizeKiB,
                    info.encodeSizeRatio * 100.0
                );
            } else {
                print_row(
                    lib::names[lib],
                    info.totalEncodeTime,
                    info.totalDecodeTime,
                    info.pixelsPerEncode,
                    info.pixelsPerDecode,
                    encRatio,
                    decRatio,
                    info.encodedSizeKiB,
                    info.encodeSizeRatio * 100.0
                );
            }
        }

        print_sep("└", "┘", "┴", "─");
    }
};

RawImage loadImage(const fs::path& file)
{
    int   width, height, channels;
    auto* data = stbi_load(file.c_str(), &width, &height, &channels, 0);
    if (data == nullptr) {
        throw std::runtime_error{ fmt::format("Error decoding file '{}' (stb)", file) };
    }

    auto size = static_cast<size_t>(width * height * channels);

    RawImage image{
        .data = { data, data + size },
        .desc = {
            .width      = static_cast<unsigned int>(width),
            .height     = static_cast<unsigned int>(height),
            .channels   = channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
            .colorspace = qoipp::Colorspace::sRGB,
        },
    };

    stbi_image_free(data);
    return image;
}

EncodeResult<> qoiEncode(const RawImage& image)
{
    qoi_desc desc{
        .width      = static_cast<unsigned int>(image.desc.width),
        .height     = static_cast<unsigned int>(image.desc.height),
        .channels   = (unsigned char)(image.desc.channels == qoipp::Channels::RGB ? 3 : 4),
        .colorspace = (unsigned char)(image.desc.colorspace == qoipp::Colorspace::sRGB ? QOI_SRGB : QOI_LINEAR
        ),
    };

    int   len;
    auto  timepoint = Clock::now();
    auto* data      = qoi_encode(image.data.data(), &desc, &len);
    auto  duration  = Clock::now() - timepoint;

    if (!data) {
        throw std::runtime_error{ "Error encoding image (qoi)" };
    }

    auto* bytePtr = reinterpret_cast<std::uint8_t*>(data);

    EncodeResult result{
        .image = {
            .data  = { bytePtr, bytePtr + len },
            .desc  = image.desc,
        },
        .time = duration,
    };

    QOI_FREE(data);
    return result;
}

DecodeResult qoiDecode(const QoiImage& image)
{
    qoi_desc desc;

    auto  timepoint = Clock::now();
    auto* data      = qoi_decode(image.data.data(), (int)image.data.size(), &desc, 0);
    auto  duration  = Clock::now() - timepoint;

    if (!data) {
        throw std::runtime_error{ "Error decoding image (qoi)" };
    }

    auto* bytePtr = reinterpret_cast<std::uint8_t*>(data);
    auto  size    = static_cast<size_t>(desc.width * desc.height * desc.channels);

    DecodeResult result{
        .image = {
            .data = { bytePtr, bytePtr + size },
            .desc = {
                .width      = desc.width,
                .height     = desc.height,
                .channels   = desc.channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
                .colorspace = desc.colorspace == QOI_SRGB ? qoipp::Colorspace::sRGB : qoipp::Colorspace::Linear,
            },
        },
        .time = duration,
    };

    QOI_FREE(data);
    return result;
}

EncodeResult<> qoixxEncode(const RawImage& image)
{
    using T = qoipp::Vec;

    qoixx::qoi::desc desc = {
        .width      = image.desc.width,
        .height     = image.desc.height,
        .channels   = std::uint8_t(image.desc.channels == qoipp::Channels::RGB ? 3 : 4),
        .colorspace = image.desc.colorspace == qoipp::Colorspace::sRGB ? qoixx::qoi::colorspace::srgb
                                                                       : qoixx::qoi::colorspace::linear,
    };

    auto timepoint = Clock::now();
    auto encoded   = qoixx::qoi::encode<T>(image.data, desc);
    auto duration  = Clock::now() - timepoint;

    return {
        .image = { encoded, image.desc },
        .time  = duration,
    };
}

EncodeResult<> qoixxDecode(const QoiImage& image)
{
    using T = qoipp::Vec;

    auto timepoint       = Clock::now();
    auto [encoded, desc] = qoixx::qoi::decode<T>(image.data);
    auto duration        = Clock::now() - timepoint;

    qoipp::ImageDesc qoippDesc = {
        .width      = desc.width,
        .height     = desc.height,
        .channels   = desc.channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
        .colorspace = desc.colorspace == qoixx::qoi::colorspace::srgb ? qoipp::Colorspace::sRGB
                                                                      : qoipp::Colorspace::Linear,
    };

    return {
        .image = { encoded, qoippDesc },
        .time  = duration,
    };
}

EncodeResult<> qoippEncode(const RawImage& image)
{
    auto timepoint = Clock::now();
    auto encoded   = qoipp::encode(image.data, image.desc);
    auto duration  = Clock::now() - timepoint;

    return {
        .image = { encoded, image.desc },
        .time  = duration,
    };
}

DecodeResult qoippDecode(const QoiImage& image)
{
    auto timepoint       = Clock::now();
    auto [decoded, desc] = qoipp::decode(image.data);
    auto duration        = Clock::now() - timepoint;

    return {
        .image = { decoded, desc },
        .time  = duration,
    };
}

EncodeResult<PngImage> stbEncode(const RawImage& image)
{
    auto timepoint = Clock::now();
    auto len       = 0;

    auto&& [data, desc] = image;
    auto encoded        = stbi_write_png_to_mem(
        reinterpret_cast<const unsigned char*>(data.data()),
        0,
        static_cast<int>(desc.width),
        static_cast<int>(desc.height),
        static_cast<int>(desc.channels),
        &len
    );

    auto duration = Clock::now() - timepoint;

    EncodeResult<PngImage> result {
        .image = {
            .data = { encoded, encoded + len },
            .desc = desc,
        },
        .time = duration,
    };

    stbi_image_free(encoded);
    return result;
}

DecodeResult stbDecode(const PngImage& image)
{
    auto timepoint = Clock::now();
    int  width, height, channels;
    auto data = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(image.data.data()),
        static_cast<int>(image.data.size()),
        &width,
        &height,
        &channels,
        0
    );

    auto duration = Clock::now() - timepoint;
    auto size     = static_cast<size_t>(width * height * channels);

    DecodeResult result{
        .image = {
            .data = { data, data + size },
            .desc = {
                .width      = static_cast<unsigned int>(width),
                .height     = static_cast<unsigned int>(height),
                .channels   = channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
                .colorspace = qoipp::Colorspace::sRGB,
            },
        },
        .time = duration,
    };

    stbi_image_free(data);
    return result;
}

EncodeResult<PngImage> fpngEncode(const RawImage& image)
{
    auto& [data, desc] = image;
    auto chan          = static_cast<unsigned int>(desc.channels);

    auto timepoint = Clock::now();
    auto encoded   = std::vector<std::uint8_t>{};
    auto success   = fpng::fpng_encode_image_to_memory(data.data(), desc.width, desc.height, chan, encoded);
    auto duration  = Clock::now() - timepoint;

    if (not success) {
        throw std::runtime_error{ "Error encoding image (fpng)" };
    }

    auto bytePtr = (encoded.data());

    return {
        .image = {
            .data = { bytePtr, bytePtr + encoded.size() },
            .desc = desc,
        },
        .time = duration,
    };
}

DecodeResult fpngDecode(const PngImage& image)
{
    auto& [data, desc] = image;

    auto timepoint = Clock::now();

    auto         decoded = std::vector<std::uint8_t>{};
    unsigned int width, height, channels;
    auto         success = fpng::fpng_decode_memory(
        data.data(),
        static_cast<unsigned int>(data.size()),
        decoded,
        width,
        height,
        channels,
        static_cast<unsigned int>(desc.channels)
    );

    auto duration = Clock::now() - timepoint;

    if (success != fpng::FPNG_DECODE_SUCCESS) {
        throw std::runtime_error{ "Error decoding image (fpng)" };
    }

    auto bytePtr = (decoded.data());

    return {
        .image = {
            .data = { bytePtr, bytePtr + decoded.size() },
            .desc = {
                .width = width,
                .height = height,
                .channels = channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
                .colorspace = qoipp::Colorspace::sRGB,
            },
        },
        .time = duration,
    };
}

BenchmarkResult benchmark(const fs::path& file, const Options& opt)
{
    fmt::println("\t>> Benchmarking '{}'", file);

    auto rawImage     = loadImage(file);
    auto qoiImage     = qoiEncode(rawImage).image;
    auto pngImageFpng = opt.fpng ? fpngEncode(rawImage).image : PngImage{};
    auto pngImageStb  = opt.stb ? stbEncode(rawImage).image : PngImage{};

    if (opt.verify) {
        const auto verify = [&]<typename T>(const T& leftImage, const T& rightImage)
            requires(std::same_as<T, RawImage> or std::same_as<T, QoiImage>)
        {
            if (leftImage.data != rightImage.data || leftImage.desc != rightImage.desc) {
                fmt::println("\t\tVerification failed for {} [skipped]", file);
                return false;
            }
            return true;
        };

        {    // qoi encode -> qoipp decode -> compare with rawImage
            fmt::println("\t\tverifying qoi   encode -> qoipp decode");
            auto [qoiEncoded, _]    = qoiEncode(rawImage);
            auto [qoippDecoded, __] = qoippDecode(qoiEncoded);
            if (!verify(qoippDecoded, rawImage)) {
                return { .file = file };
            }
        }

        {    // qoipp encode -> qoi decode -> compare with rawImage
            fmt::println("\t\tverifying qoipp encode -> qoi   decode");
            auto [qoippEncoded, _] = qoippEncode(rawImage);
            auto [qoiDecoded, __]  = qoiDecode(qoippEncoded);
            if (!verify(qoiDecoded, rawImage)) {
                return { .file = file };
            }
        }

        {    // qoi decode -> qoipp encode -> compare with qoiImage
            fmt::println("\t\tverifying qoi   decode -> qoipp encode");
            auto [qoiDecoded, _]    = qoiDecode(qoiImage);
            auto [qoippEncoded, __] = qoippEncode(qoiDecoded);
            if (!verify(qoippEncoded, qoiImage)) {
                return { .file = file };
            }
        }

        {    // qoipp decode -> qoi encode -> compare with qoiImage
            fmt::println("\t\tverifying qoipp decode -> qoi   encode");
            auto [qoippDecoded, _] = qoippDecode(qoiImage);
            auto [qoiEncoded, __]  = qoiEncode(qoippDecoded);
            if (!verify(qoiEncoded, qoiImage)) {
                return { .file = file };
            }
        }
    }

    auto benchmarkImpl = [&](auto func, const auto& image) {
        auto [_, time] = func(image);
        if (opt.warmup) {
            for (auto i = 0; i < 3; ++i) {
                func(image);
            }
        }

        Duration    total = Duration::zero();
        std::size_t size  = 0;
        for (auto run = opt.runs; run-- > 0;) {
            auto [coded, time]  = func(image);
            total              += time;
            size                = coded.data.size();
        }

        return std::make_pair(total / opt.runs, size);
    };

    // the benchmark starts here

    BenchmarkResult result = {
        .desc    = qoiImage.desc,
        .file    = file,
        .rawSize = rawImage.data.size(),
    };

    fmt::println("\t\tbenchmark");

    if (opt.encode) {
        auto [qoiTime, qoiSize]     = benchmarkImpl(qoiEncode, rawImage);
        auto [qoixxTime, qoixxSize] = benchmarkImpl(qoixxEncode, rawImage);
        auto [qoippTime, qoippSize] = benchmarkImpl(qoippEncode, rawImage);

        result.libsInfo[lib::Lib::qoi].encodeTime  = qoiTime;
        result.libsInfo[lib::Lib::qoi].encodedSize = qoiSize;

        result.libsInfo[lib::Lib::qoixx].encodeTime  = qoixxTime;
        result.libsInfo[lib::Lib::qoixx].encodedSize = qoixxSize;

        result.libsInfo[lib::Lib::qoipp].encodeTime  = qoippTime;
        result.libsInfo[lib::Lib::qoipp].encodedSize = qoippSize;

        if (opt.stb) {
            auto [stbTime, stbSize] = benchmarkImpl(stbEncode, rawImage);

            result.libsInfo[lib::Lib::stb].encodeTime  = stbTime;
            result.libsInfo[lib::Lib::stb].encodedSize = stbSize;
        }
        if (opt.fpng) {
            auto [fpngTime, fpngSize] = benchmarkImpl(fpngEncode, rawImage);

            result.libsInfo[lib::Lib::fpng].encodeTime  = fpngTime;
            result.libsInfo[lib::Lib::fpng].encodedSize = fpngSize;
        }
    }

    if (opt.decode) {
        auto [qoiTime, _]     = benchmarkImpl(qoiDecode, qoiImage);
        auto [qoixxTime, __]  = benchmarkImpl(qoixxDecode, qoiImage);
        auto [qoippTime, ___] = benchmarkImpl(qoippDecode, qoiImage);

        result.libsInfo[lib::Lib::qoi].decodeTime   = qoiTime;
        result.libsInfo[lib::Lib::qoixx].decodeTime = qoixxTime;
        result.libsInfo[lib::Lib::qoipp].decodeTime = qoippTime;

        if (opt.stb) {
            auto [stbTime, _] = benchmarkImpl(stbDecode, pngImageStb);

            result.libsInfo[lib::Lib::stb].decodeTime = stbTime;
        }

        if (opt.fpng) {
            auto [fpngTime, _] = benchmarkImpl(fpngDecode, pngImageFpng);

            result.libsInfo[lib::Lib::fpng].decodeTime = fpngTime;
        }
    }

    return result;
}

std::vector<BenchmarkResult> benchmarkDirectory(const fs::path& path, const Options& opt)
{
    std::vector<BenchmarkResult> results;

    if (opt.recurse) {
        fmt::println(">> Benchmarking {} (recurse)...", path / "**/*.png");

        for (const auto& path : fs::recursive_directory_iterator{ path }) {
            if (fs::is_regular_file(path) && path.path().extension() == ".png") {
                auto result = benchmark(path, opt);
                result.print(opt.color);
                results.push_back(result);
            }
        }
    } else {
        fmt::println(">> Benchmarking {}...", path / "*.png");

        for (const auto& path : fs::directory_iterator{ path }) {
            if (fs::is_regular_file(path) && path.path().extension() == ".png") {
                auto result = benchmark(path, opt);
                result.print(opt.color);
                results.push_back(result);
            }
        }
    }

    fmt::println("\t>> Benchmarking '{}' done!", path);

    return results;
}

BenchmarkResult averageResults(std::span<const BenchmarkResult> results)
{
    auto average = BenchmarkResult{
        .desc = {
            .width      = 0,
            .height     = 0,
            .channels   = {},
            .colorspace = {},
        },
        .file    = "Summary",
        .rawSize = 0,
    };

    auto add = [&](const std::map<lib::Lib, BenchmarkResult::LibInfo>& values) {
        for (auto&& [k, v] : values) {
            average.libsInfo[k].encodeTime  += v.encodeTime;
            average.libsInfo[k].decodeTime  += v.decodeTime;
            average.libsInfo[k].encodedSize += v.encodedSize;
        }
    };

    for (auto&& result : results) {
        average.rawSize += result.rawSize;
        add(result.libsInfo);
    }

    for (auto& [_, v] : average.libsInfo) {
        v.encodeTime /= static_cast<long>(results.size());
        v.decodeTime /= static_cast<long>(results.size());
    }

    return average;
}

int main(int argc, char* argv[])
try {
    CLI::App app{ "Qoibench - Benchmarking tool for QOI" };
    Options  opt;
    fs::path dirpath;

    app.add_option("dir", dirpath, "Directory to benchmark")->required();
    opt.configure(app);

    if (argc <= 1) {
        fmt::println("{}", app.help());
        return 1;
    }

    CLI11_PARSE(app, argc, argv);

    opt.print();

    if (!fs::exists(dirpath)) {
        fmt::println("'{}' directory does not exist", dirpath);
        return 1;
    }

    fpng::fpng_init();

    if (fs::is_directory(dirpath)) {
        auto results = benchmarkDirectory(dirpath, opt);
        auto summary = averageResults(results);
        summary.print(opt.color);
    } else if (fs::is_regular_file(dirpath) and dirpath.extension() == ".png") {
        auto result = benchmark(dirpath, opt);
        result.print(opt.color);
    } else {
        fmt::println("'{}' is not a directory nor a png file", dirpath);
        return 1;
    }

} catch (std::exception& e) {
    fmt::println("Exception occurred: {}", e.what());
    return 1;
}
