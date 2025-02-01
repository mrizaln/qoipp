#include <limits>
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
#include <string>
#include <utility>

namespace fs = std::filesystem;

using Clock     = std::chrono::high_resolution_clock;
using TimePoint = Clock::time_point;
using Duration  = Clock::duration;    // nano seconds

struct FmtFill
{
    const char* m_value;
    std::size_t m_width;
};

template <>
struct fmt::formatter<FmtFill>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
    constexpr auto format(FmtFill f, format_context& ctx)
    {
        auto&& out = ctx.out();
        for (std::size_t i = 0; i < f.m_width; ++i) {
            fmt::format_to(out, "{}", f.m_value);
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
    qoipp::ByteVec   m_data;
    qoipp::ImageDesc m_desc;
};

struct QoiImage
{
    qoipp::ByteVec   m_data;
    qoipp::ImageDesc m_desc;
};

struct PngImage
{
    qoipp::ByteVec   m_data;
    qoipp::ImageDesc m_desc;
};

template <typename Image = QoiImage>
struct EncodeResult
{
    Image    m_image;
    Duration m_time;
};

struct DecodeResult
{
    RawImage m_image;
    Duration m_time;
};

auto descString(const qoipp::ImageDesc& desc)
{
    return fmt::format(
        "{}x{} ({}|{})",
        desc.m_width,
        desc.m_height,
        fmt::underlying(desc.m_channels),
        fmt::underlying(desc.m_colorspace)
    );
}

struct Options
{
    bool         m_warmup     = true;
    bool         m_verify     = true;
    bool         m_reference  = true;
    bool         m_encode     = true;
    bool         m_decode     = true;
    bool         m_recurse    = true;
    bool         m_onlyTotals = false;
    bool         m_color      = true;
    bool         m_stb        = true;
    bool         m_fpng       = true;
    unsigned int m_runs       = 1;

    void configure(CLI::App& app)
    {
        app.add_option("runs", m_runs, "Number of runs")->default_val(m_runs)->check(CLI::PositiveNumber);

        app.add_flag("!--no-warmup", m_warmup, "Don't perform a warmup run");
        app.add_flag("!--no-verify", m_verify, "Don't verify qoi roundtrip");
        app.add_flag("!--no-reference", m_reference, "Don't run reference implementation");
        app.add_flag("!--no-encode", m_encode, "Don't run encoders");
        app.add_flag("!--no-decode", m_decode, "Don't run decoders");
        app.add_flag("!--no-recurse", m_recurse, "Don't descend into directories");
        app.add_flag("!--no-color", m_color, "Don't print with color");
        app.add_flag("!--no-stb", m_stb, "Don't benchmark stb");
        app.add_flag("!--no-fpng", m_fpng, "Don't benchmark fpng");
        app.add_flag("--only-totals", m_onlyTotals, "Don't print individual image results");
    }

    void print()
    {
        fmt::println("Options:");
        fmt::println("\t- runs      : {}", m_runs);
        fmt::println("\t- warmup    : {}", m_warmup);
        fmt::println("\t- verify    : {}", m_verify);
        fmt::println("\t- reference : {}", m_reference);
        fmt::println("\t- encode    : {}", m_encode);
        fmt::println("\t- decode    : {}", m_decode);
        fmt::println("\t- recurse   : {}", m_recurse);
        fmt::println("\t- color     : {}", m_color);
        fmt::println("\t- stb       : {}", m_stb);
        fmt::println("\t- fpng      : {}", m_fpng);
        fmt::println("\t- onlytotals: {}", m_onlyTotals);
    }
};

struct BenchmarkResult
{
    struct LibInfo
    {
        Duration    m_encodeTime  = {};
        Duration    m_decodeTime  = {};
        std::size_t m_encodedSize = 0;
    };

    qoipp::ImageDesc m_desc    = {};
    fs::path         m_file    = {};
    std::size_t      m_rawSize = 0;

    std::map<lib::Lib, LibInfo> m_libsInfo = {};

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
            float       m_totalEncodeTime;
            float       m_totalDecodeTime;
            float       m_pixelsPerEncode;
            float       m_pixelsPerDecode;
            std::size_t m_encodedSizeKiB;
            float       m_encodeSizeRatio;
        };

        auto pixelCount = m_desc.m_width * m_desc.m_height;

        std::map<Lib, Printed> printed;
        for (const auto& [lib, info] : m_libsInfo) {
            printed.emplace(
                lib,
                Printed{
                    .m_totalEncodeTime = toMillis(info.m_encodeTime).count(),
                    .m_totalDecodeTime = toMillis(info.m_decodeTime).count(),
                    .m_pixelsPerEncode = (float)pixelCount / toMicros(info.m_encodeTime).count(),
                    .m_pixelsPerDecode = (float)pixelCount / toMicros(info.m_decodeTime).count(),
                    .m_encodedSizeKiB  = info.m_encodedSize / 1000,
                    .m_encodeSizeRatio = (float)info.m_encodedSize / (float)m_rawSize,
                }
            );
        }

        const auto [width, height, channels, _] = m_desc;
        fmt::println(
            "File: '{}' [{} x {} ({})]",
            m_file,
            width,
            height,
            channels == qoipp::Channels::RGB ? "RGB" : "RGBA"
        );

        if (m_libsInfo.empty()) {
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
                auto qoiEnc  = qoi.m_totalEncodeTime;
                auto qoiDec  = qoi.m_totalDecodeTime;
                auto infoEnc = info.m_totalEncodeTime;
                auto infoDec = info.m_totalDecodeTime;

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
                    info.m_totalEncodeTime,
                    info.m_totalDecodeTime,
                    info.m_pixelsPerEncode,
                    info.m_pixelsPerDecode,
                    fmt::styled(encRatio, fmt::bg(encRatio > 0 ? fmt::color::orange_red : fmt::color::green)),
                    fmt::styled(decRatio, fmt::bg(decRatio > 0 ? fmt::color::orange_red : fmt::color::green)),
                    info.m_encodedSizeKiB,
                    info.m_encodeSizeRatio * 100.0
                );
            } else {
                print_row(
                    lib::names[lib],
                    info.m_totalEncodeTime,
                    info.m_totalDecodeTime,
                    info.m_pixelsPerEncode,
                    info.m_pixelsPerDecode,
                    encRatio,
                    decRatio,
                    info.m_encodedSizeKiB,
                    info.m_encodeSizeRatio * 100.0
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

    auto* bytePtr = reinterpret_cast<std::byte*>(data);
    auto  size    = static_cast<size_t>(width * height * channels);

    RawImage image{
        .m_data = { bytePtr, bytePtr + size },
        .m_desc = {
            .m_width      = static_cast<unsigned int>(width),
            .m_height     = static_cast<unsigned int>(height),
            .m_channels   = channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
            .m_colorspace = qoipp::Colorspace::sRGB,
        },
    };

    stbi_image_free(data);
    return image;
}

EncodeResult<> qoiEncode(const RawImage& image)
{
    qoi_desc desc{
        .width      = static_cast<unsigned int>(image.m_desc.m_width),
        .height     = static_cast<unsigned int>(image.m_desc.m_height),
        .channels   = (unsigned char)(image.m_desc.m_channels == qoipp::Channels::RGB ? 3 : 4),
        .colorspace = (unsigned char)(image.m_desc.m_colorspace == qoipp::Colorspace::sRGB ? QOI_SRGB
                                                                                           : QOI_LINEAR),
    };

    int   len;
    auto  timepoint = Clock::now();
    auto* data      = qoi_encode(image.m_data.data(), &desc, &len);
    auto  duration  = Clock::now() - timepoint;

    if (!data) {
        throw std::runtime_error{ "Error encoding image (qoi)" };
    }

    auto* bytePtr = reinterpret_cast<std::byte*>(data);

    EncodeResult result{
        .m_image = {
            .m_data  = { bytePtr, bytePtr + len },
            .m_desc  = image.m_desc,
        },
        .m_time = duration,
    };

    QOI_FREE(data);
    return result;
}

DecodeResult qoiDecode(const QoiImage& image)
{
    qoi_desc desc;

    auto  timepoint = Clock::now();
    auto* data      = qoi_decode(image.m_data.data(), (int)image.m_data.size(), &desc, 0);
    auto  duration  = Clock::now() - timepoint;

    if (!data) {
        throw std::runtime_error{ "Error decoding image (qoi)" };
    }

    auto* bytePtr = reinterpret_cast<std::byte*>(data);
    auto  size    = static_cast<size_t>(desc.width * desc.height * desc.channels);

    DecodeResult result{
        .m_image = {
            .m_data = { bytePtr, bytePtr + size },
            .m_desc = {
                .m_width      = desc.width,
                .m_height     = desc.height,
                .m_channels   = desc.channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
                .m_colorspace = desc.colorspace == QOI_SRGB ? qoipp::Colorspace::sRGB : qoipp::Colorspace::Linear,
            },
        },
        .m_time = duration,
    };

    QOI_FREE(data);
    return result;
}

EncodeResult<> qoixxEncode(const RawImage& image)
{
    using T = qoipp::ByteVec;

    qoixx::qoi::desc desc = {
        .width      = image.m_desc.m_width,
        .height     = image.m_desc.m_height,
        .channels   = std::uint8_t(image.m_desc.m_channels == qoipp::Channels::RGB ? 3 : 4),
        .colorspace = image.m_desc.m_colorspace == qoipp::Colorspace::sRGB ? qoixx::qoi::colorspace::srgb
                                                                           : qoixx::qoi::colorspace::linear,
    };

    auto timepoint = Clock::now();
    auto encoded   = qoixx::qoi::encode<T>(image.m_data, desc);
    auto duration  = Clock::now() - timepoint;

    return {
        .m_image = { encoded, image.m_desc },
        .m_time  = duration,
    };
}

EncodeResult<> qoixxDecode(const QoiImage& image)
{
    using T = qoipp::ByteVec;

    auto timepoint       = Clock::now();
    auto [encoded, desc] = qoixx::qoi::decode<T>(image.m_data);
    auto duration        = Clock::now() - timepoint;

    qoipp::ImageDesc qoippDesc = {
        .m_width      = desc.width,
        .m_height     = desc.height,
        .m_channels   = desc.channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
        .m_colorspace = desc.colorspace == qoixx::qoi::colorspace::srgb ? qoipp::Colorspace::sRGB
                                                                        : qoipp::Colorspace::Linear,
    };

    return {
        .m_image = { encoded, qoippDesc },
        .m_time  = duration,
    };
}

EncodeResult<> qoippEncode(const RawImage& image)
{
    auto timepoint = Clock::now();
    auto encoded   = qoipp::encode(image.m_data, image.m_desc);
    auto duration  = Clock::now() - timepoint;

    return {
        .m_image = { encoded, image.m_desc },
        .m_time  = duration,
    };
}

DecodeResult qoippDecode(const QoiImage& image)
{
    auto timepoint       = Clock::now();
    auto [decoded, desc] = qoipp::decode(image.m_data);
    auto duration        = Clock::now() - timepoint;

    return {
        .m_image = { decoded, desc },
        .m_time  = duration,
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
        static_cast<int>(desc.m_width),
        static_cast<int>(desc.m_height),
        static_cast<int>(desc.m_channels),
        &len
    );

    auto duration = Clock::now() - timepoint;

    auto bytePtr = reinterpret_cast<std::byte*>(encoded);
    EncodeResult<PngImage> result {
        .m_image = {
            .m_data = { bytePtr, bytePtr + len },
            .m_desc = desc,
        },
        .m_time = duration,
    };

    stbi_image_free(encoded);
    return result;
}

DecodeResult stbDecode(const PngImage& image)
{
    auto timepoint = Clock::now();
    int  width, height, channels;
    auto data = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(image.m_data.data()),
        static_cast<int>(image.m_data.size()),
        &width,
        &height,
        &channels,
        0
    );

    auto duration = Clock::now() - timepoint;

    auto bytePtr = reinterpret_cast<std::byte*>(data);
    auto size    = static_cast<size_t>(width * height * channels);

    DecodeResult result{
        .m_image = {
            .m_data = { bytePtr, bytePtr + size },
            .m_desc = {
                .m_width      = static_cast<unsigned int>(width),
                .m_height     = static_cast<unsigned int>(height),
                .m_channels   = channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
                .m_colorspace = qoipp::Colorspace::sRGB,
            },
        },
        .m_time = duration,
    };

    stbi_image_free(data);
    return result;
}

EncodeResult<PngImage> fpngEncode(const RawImage& image)
{
    auto& [data, desc] = image;
    auto chan          = static_cast<unsigned int>(desc.m_channels);

    auto timepoint = Clock::now();
    auto encoded   = std::vector<std::uint8_t>{};
    auto success = fpng::fpng_encode_image_to_memory(data.data(), desc.m_width, desc.m_height, chan, encoded);
    auto duration = Clock::now() - timepoint;

    if (not success) {
        throw std::runtime_error{ "Error encoding image (fpng)" };
    }

    auto bytePtr = reinterpret_cast<std::byte*>(encoded.data());

    return {
        .m_image = {
            .m_data = { bytePtr, bytePtr + encoded.size() },
            .m_desc = desc,
        },
        .m_time = duration,
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
        static_cast<unsigned int>(desc.m_channels)
    );

    auto duration = Clock::now() - timepoint;

    if (success != fpng::FPNG_DECODE_SUCCESS) {
        throw std::runtime_error{ "Error decoding image (fpng)" };
    }

    auto bytePtr = reinterpret_cast<std::byte*>(decoded.data());

    return {
        .m_image = {
            .m_data = { bytePtr, bytePtr + decoded.size() },
            .m_desc = {
                .m_width = width,
                .m_height = height,
                .m_channels = channels == 3 ? qoipp::Channels::RGB : qoipp::Channels::RGBA,
                .m_colorspace = qoipp::Colorspace::sRGB,
            },
        },
        .m_time = duration,
    };
}

BenchmarkResult benchmark(const fs::path& file, const Options& opt)
{
    fmt::println("\t>> Benchmarking '{}'", file);

    auto rawImage     = loadImage(file);
    auto qoiImage     = qoiEncode(rawImage).m_image;
    auto pngImageFpng = opt.m_fpng ? fpngEncode(rawImage).m_image : PngImage{};
    auto pngImageStb  = opt.m_stb ? stbEncode(rawImage).m_image : PngImage{};

    if (opt.m_verify) {
        const auto verify = [&]<typename T>(const T& leftImage, const T& rightImage)
            requires(std::same_as<T, RawImage> or std::same_as<T, QoiImage>)
        {
            if (leftImage.m_data != rightImage.m_data || leftImage.m_desc != rightImage.m_desc) {
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
                return { .m_file = file };
            }
        }

        {    // qoipp encode -> qoi decode -> compare with rawImage
            fmt::println("\t\tverifying qoipp encode -> qoi   decode");
            auto [qoippEncoded, _] = qoippEncode(rawImage);
            auto [qoiDecoded, __]  = qoiDecode(qoippEncoded);
            if (!verify(qoiDecoded, rawImage)) {
                return { .m_file = file };
            }
        }

        {    // qoi decode -> qoipp encode -> compare with qoiImage
            fmt::println("\t\tverifying qoi   decode -> qoipp encode");
            auto [qoiDecoded, _]    = qoiDecode(qoiImage);
            auto [qoippEncoded, __] = qoippEncode(qoiDecoded);
            if (!verify(qoippEncoded, qoiImage)) {
                return { .m_file = file };
            }
        }

        {    // qoipp decode -> qoi encode -> compare with qoiImage
            fmt::println("\t\tverifying qoipp decode -> qoi   encode");
            auto [qoippDecoded, _] = qoippDecode(qoiImage);
            auto [qoiEncoded, __]  = qoiEncode(qoippDecoded);
            if (!verify(qoiEncoded, qoiImage)) {
                return { .m_file = file };
            }
        }
    }

    auto benchmarkImpl = [&](auto func, const auto& image) {
        auto [_, time] = func(image);
        if (opt.m_warmup) {
            for (auto i = 0; i < 3; ++i) {
                func(image);
            }
        }

        Duration    total = Duration::zero();
        std::size_t size  = 0;
        for (auto run = opt.m_runs; run-- > 0;) {
            auto [coded, time]  = func(image);
            total              += time;
            size                = coded.m_data.size();
        }

        return std::make_pair(total / opt.m_runs, size);
    };

    // the benchmark starts here

    BenchmarkResult result = {
        .m_desc    = qoiImage.m_desc,
        .m_file    = file,
        .m_rawSize = rawImage.m_data.size(),
    };

    fmt::println("\t\tbenchmark");

    if (opt.m_encode) {
        auto [qoiTime, qoiSize]     = benchmarkImpl(qoiEncode, rawImage);
        auto [qoixxTime, qoixxSize] = benchmarkImpl(qoixxEncode, rawImage);
        auto [qoippTime, qoippSize] = benchmarkImpl(qoippEncode, rawImage);

        result.m_libsInfo[lib::Lib::qoi].m_encodeTime  = qoiTime;
        result.m_libsInfo[lib::Lib::qoi].m_encodedSize = qoiSize;

        result.m_libsInfo[lib::Lib::qoixx].m_encodeTime  = qoixxTime;
        result.m_libsInfo[lib::Lib::qoixx].m_encodedSize = qoixxSize;

        result.m_libsInfo[lib::Lib::qoipp].m_encodeTime  = qoippTime;
        result.m_libsInfo[lib::Lib::qoipp].m_encodedSize = qoippSize;

        if (opt.m_stb) {
            auto [stbTime, stbSize] = benchmarkImpl(stbEncode, rawImage);

            result.m_libsInfo[lib::Lib::stb].m_encodeTime  = stbTime;
            result.m_libsInfo[lib::Lib::stb].m_encodedSize = stbSize;
        }
        if (opt.m_fpng) {
            auto [fpngTime, fpngSize] = benchmarkImpl(fpngEncode, rawImage);

            result.m_libsInfo[lib::Lib::fpng].m_encodeTime  = fpngTime;
            result.m_libsInfo[lib::Lib::fpng].m_encodedSize = fpngSize;
        }
    }

    if (opt.m_decode) {
        auto [qoiTime, _]     = benchmarkImpl(qoiDecode, qoiImage);
        auto [qoixxTime, __]  = benchmarkImpl(qoixxDecode, qoiImage);
        auto [qoippTime, ___] = benchmarkImpl(qoippDecode, qoiImage);

        result.m_libsInfo[lib::Lib::qoi].m_decodeTime   = qoiTime;
        result.m_libsInfo[lib::Lib::qoixx].m_decodeTime = qoixxTime;
        result.m_libsInfo[lib::Lib::qoipp].m_decodeTime = qoippTime;

        if (opt.m_stb) {
            auto [stbTime, _] = benchmarkImpl(stbDecode, pngImageStb);

            result.m_libsInfo[lib::Lib::stb].m_decodeTime = stbTime;
        }

        if (opt.m_fpng) {
            auto [fpngTime, _] = benchmarkImpl(fpngDecode, pngImageFpng);

            result.m_libsInfo[lib::Lib::fpng].m_decodeTime = fpngTime;
        }
    }

    return result;
}

std::vector<BenchmarkResult> benchmarkDirectory(const fs::path& path, const Options& opt)
{
    std::vector<BenchmarkResult> results;

    if (opt.m_recurse) {
        fmt::println(">> Benchmarking {} (recurse)...", path / "**/*.png");

        for (const auto& path : fs::recursive_directory_iterator{ path }) {
            if (fs::is_regular_file(path) && path.path().extension() == ".png") {
                auto result = benchmark(path, opt);
                result.print(opt.m_color);
                results.push_back(result);
            }
        }
    } else {
        fmt::println(">> Benchmarking {}...", path / "*.png");

        for (const auto& path : fs::directory_iterator{ path }) {
            if (fs::is_regular_file(path) && path.path().extension() == ".png") {
                auto result = benchmark(path, opt);
                result.print(opt.m_color);
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
        .m_desc = {
            .m_width      = 0,
            .m_height     = 0,
            .m_channels   = {},
            .m_colorspace = {},
        },
        .m_file    = "Summary",
        .m_rawSize = 0,
    };

    auto add = [&](const std::map<lib::Lib, BenchmarkResult::LibInfo>& values) {
        for (auto&& [k, v] : values) {
            average.m_libsInfo[k].m_encodeTime  += v.m_encodeTime;
            average.m_libsInfo[k].m_decodeTime  += v.m_decodeTime;
            average.m_libsInfo[k].m_encodedSize += v.m_encodedSize;
        }
    };

    for (auto&& result : results) {
        average.m_rawSize += result.m_rawSize;
        add(result.m_libsInfo);
    }

    for (auto& [_, v] : average.m_libsInfo) {
        v.m_encodeTime /= static_cast<long>(results.size());
        v.m_decodeTime /= static_cast<long>(results.size());
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
        summary.print(opt.m_color);
    } else if (fs::is_regular_file(dirpath) and dirpath.extension() == ".png") {
        auto result = benchmark(dirpath, opt);
        result.print(opt.m_color);
    } else {
        fmt::println("'{}' is not a directory nor a png file", dirpath);
        return 1;
    }

} catch (std::exception& e) {
    fmt::println("Exception occurred: {}", e.what());
    return 1;
}
