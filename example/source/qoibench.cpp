#include <qoipp/qoipp.hpp>
#define QOI_IMPLEMENTATION
#include <fpng.h>
#include <qoi.h>
#include <qoixx.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <CLI/CLI.hpp>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/std.h>

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
    qoipp::ByteVec data;
    qoipp::Desc    desc;
};

struct QoiImage
{
    qoipp::ByteVec data;
    qoipp::Desc    desc;
};

struct PngImage
{
    qoipp::ByteVec data;
    qoipp::Desc    desc;
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

auto desc_string(const qoipp::Desc& desc)
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
    bool         warmup      = true;
    bool         verify      = true;
    bool         encode      = true;
    bool         decode      = true;
    bool         recurse     = true;
    bool         only_totals = false;
    bool         color       = true;
    bool         stb         = true;
    bool         fpng        = true;
    bool         qoi         = true;
    bool         qoixx       = true;
    bool         qoipp       = true;
    unsigned int runs        = 1;

    void configure(CLI::App& app)
    {
        app.add_option("runs", runs, "Number of runs")->default_val(runs)->check(CLI::PositiveNumber);

        app.add_flag("!--no-warmup", warmup, "Don't perform a warmup run");
        app.add_flag("!--no-verify", verify, "Don't verify qoi roundtrip");
        app.add_flag("!--no-encode", encode, "Don't run encoders");
        app.add_flag("!--no-decode", decode, "Don't run decoders");
        app.add_flag("!--no-recurse", recurse, "Don't descend into directories");
        app.add_flag("!--no-color", color, "Don't print with color");
        app.add_flag("!--no-stb", stb, "Don't benchmark stb");
        app.add_flag("!--no-fpng", fpng, "Don't benchmark fpng");
        app.add_flag("!--no-qoi", qoi, "Don't benchmark qoi");
        app.add_flag("!--no-qoixx", qoixx, "Don't benchmark qoixx");
        app.add_flag("!--no-qoipp", qoipp, "Don't benchmark qoipp");
        app.add_flag("--only-totals", only_totals, "Don't print individual image results");
    }

    void print()
    {
        fmt::println("Options:");
        fmt::println("\t- runs      : {}", runs);
        fmt::println("\t- warmup    : {}", warmup);
        fmt::println("\t- verify    : {}", verify);
        fmt::println("\t- encode    : {}", encode);
        fmt::println("\t- decode    : {}", decode);
        fmt::println("\t- recurse   : {}", recurse);
        fmt::println("\t- color     : {}", color);
        fmt::println("\t- stb       : {}", stb);
        fmt::println("\t- fpng      : {}", fpng);
        fmt::println("\t- qoi       : {}", qoi);
        fmt::println("\t- qoixx     : {}", qoixx);
        fmt::println("\t- qoipp     : {}", qoipp);
        fmt::println("\t- onlytotals: {}", only_totals);
    }
};

struct BenchmarkResult
{
    struct LibInfo
    {
        Duration    encode_time  = {};
        Duration    decode_time  = {};
        std::size_t encoded_size = 0;
    };

    qoipp::Desc desc     = {};
    fs::path    file     = {};
    std::size_t raw_size = 0;

    std::map<lib::Lib, LibInfo> libs_info = {};

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
        requires (sizeof...(c) == 9)
    {
        fmt::println(
            "┃ {:^8} ┃ {:^9} ┃ {:^9} ┃ {:^12} ┃ {:^12} ┃ {:^6} ┃ {:^6} ┃ {:^10} ┃ {:^8} ┃",
            std::forward<decltype(c)>(c)...
        );
    };

    void print_row(auto&&... c) const
        requires (sizeof...(c) == 9)
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
        using Lib    = lib::Lib;
        using Millis = std::chrono::duration<float, std::milli>;
        using Micros = std::chrono::duration<float, std::micro>;

        const auto to_millis = std::chrono::duration_cast<Millis, Duration::rep, Duration::period>;
        const auto to_micros = std::chrono::duration_cast<Micros, Duration::rep, Duration::period>;

        struct Printed
        {
            float       total_encode_time;
            float       total_decode_time;
            float       pixels_per_encode;
            float       pixels_per_decode;
            std::size_t encode_size_KiB;
            float       encode_size_ratio;
        };

        const auto pixel_count = desc.width * desc.height;

        auto printed = std::map<Lib, Printed>{};
        for (const auto& [lib, info] : libs_info) {
            printed.emplace(
                lib,
                Printed{
                    .total_encode_time = to_millis(info.encode_time).count(),
                    .total_decode_time = to_millis(info.decode_time).count(),
                    .pixels_per_encode = (float)pixel_count / to_micros(info.encode_time).count(),
                    .pixels_per_decode = (float)pixel_count / to_micros(info.decode_time).count(),
                    .encode_size_KiB   = info.encoded_size / 1000,
                    .encode_size_ratio = (float)info.encoded_size / (float)raw_size,
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

        if (libs_info.empty()) {
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

        const auto get_codec_ratio = [&](const Printed& info) -> std::pair<int, int> {
            const auto percent = [](auto old, auto neww) { return (neww - old) / old * 100; };

            if (printed.contains(lib::Lib::qoi)) {
                auto qoi      = printed[lib::Lib::qoi];
                auto qoi_enc  = qoi.total_encode_time;
                auto qoi_dec  = qoi.total_decode_time;
                auto info_enc = info.total_encode_time;
                auto info_dec = info.total_decode_time;

                if (qoi_enc <= std::numeric_limits<float>::epsilon()) {
                    return { 0, percent(qoi_dec, info_dec) };
                } else if (qoi_dec <= std::numeric_limits<float>::epsilon()) {
                    return { percent(qoi_enc, info_enc), 0 };
                } else {
                    return { percent(qoi_enc, info_enc), percent(qoi_dec, info_dec) };
                }
            } else {
                return { 0, 0 };
            }
        };

        for (const auto& [lib, info] : printed) {
            auto [enc, dec] = get_codec_ratio(info);
            if (color) {
                print_row(
                    lib::names[lib],
                    info.total_encode_time,
                    info.total_decode_time,
                    info.pixels_per_encode,
                    info.pixels_per_decode,
                    fmt::styled(enc, fmt::bg(enc > 0 ? fmt::color::orange_red : fmt::color::green)),
                    fmt::styled(dec, fmt::bg(dec > 0 ? fmt::color::orange_red : fmt::color::green)),
                    info.encode_size_KiB,
                    info.encode_size_ratio * 100.0
                );
            } else {
                print_row(
                    lib::names[lib],
                    info.total_encode_time,
                    info.total_decode_time,
                    info.pixels_per_encode,
                    info.pixels_per_decode,
                    enc,
                    dec,
                    info.encode_size_KiB,
                    info.encode_size_ratio * 100.0
                );
            }
        }

        print_sep("└", "┘", "┴", "─");
    }
};

RawImage load_image(const fs::path& file)
{
    int   width, height, channels;
    auto* data = stbi_load(file.c_str(), &width, &height, &channels, 0);
    if (data == nullptr) {
        throw std::runtime_error{ fmt::format("Error decoding file '{}' (stb)", file) };
    }

    auto size          = static_cast<size_t>(width * height * channels);
    auto channels_real = qoipp::to_channels(channels);
    if (not channels_real) {
        throw std::runtime_error{ fmt::format("Number of channels ({}) is not supported (stb)", channels) };
    }

    auto image = RawImage{
        .data = { data, data + size },
        .desc = {
            .width      = static_cast<unsigned int>(width),
            .height     = static_cast<unsigned int>(height),
            .channels   = *channels_real,
            .colorspace = qoipp::Colorspace::sRGB,
        },
    };

    stbi_image_free(data);
    return image;
}

EncodeResult<> qoi_encode_wrapper(const RawImage& image)
{
    auto desc = qoi_desc{
        .width      = static_cast<unsigned int>(image.desc.width),
        .height     = static_cast<unsigned int>(image.desc.height),
        .channels   = static_cast<unsigned char>(image.desc.channels),
        .colorspace = static_cast<unsigned char>(image.desc.colorspace),
    };

    int   len;
    auto  timepoint = Clock::now();
    auto* data      = qoi_encode(image.data.data(), &desc, &len);
    auto  duration  = Clock::now() - timepoint;

    if (!data) {
        throw std::runtime_error{ "Error encoding image (qoi)" };
    }

    auto* byte_ptr = reinterpret_cast<std::uint8_t*>(data);

    auto result = EncodeResult{
        .image = {
            .data  = { byte_ptr, byte_ptr + len },
            .desc  = image.desc,
        },
        .time = duration,
    };

    QOI_FREE(data);
    return result;
}

DecodeResult qoi_decode_wrapper(const QoiImage& image)
{
    qoi_desc desc;

    auto  timepoint = Clock::now();
    auto* data      = qoi_decode(image.data.data(), (int)image.data.size(), &desc, 0);
    auto  duration  = Clock::now() - timepoint;

    if (!data) {
        throw std::runtime_error{ "Error decoding image (qoi)" };
    }

    auto* byte_ptr = reinterpret_cast<std::uint8_t*>(data);
    auto  size     = static_cast<size_t>(desc.width * desc.height * desc.channels);

    auto result = DecodeResult{
        .image = {
            .data = { byte_ptr, byte_ptr + size },
            .desc = {
                .width      = desc.width,
                .height     = desc.height,
                .channels   = qoipp::to_channels(desc.channels).value(),        // always RGB or RGBA
                .colorspace = desc.colorspace == QOI_SRGB ? qoipp::Colorspace::sRGB : qoipp::Colorspace::Linear,
            },
        },
        .time = duration,
    };

    QOI_FREE(data);
    return result;
}

EncodeResult<> qoixx_encode(const RawImage& image)
{
    using T = qoipp::ByteVec;

    auto desc = qoixx::qoi::desc{
        .width      = image.desc.width,
        .height     = image.desc.height,
        .channels   = static_cast<std::uint8_t>(image.desc.channels),
        .colorspace = static_cast<qoixx::qoi::colorspace>(image.desc.colorspace),
    };

    auto timepoint = Clock::now();
    auto encoded   = qoixx::qoi::encode<T>(image.data, desc);
    auto duration  = Clock::now() - timepoint;

    return {
        .image = { encoded, image.desc },
        .time  = duration,
    };
}

EncodeResult<> qoixx_decode(const QoiImage& image)
{
    using T = qoipp::ByteVec;

    auto timepoint       = Clock::now();
    auto [encoded, desc] = qoixx::qoi::decode<T>(image.data);
    auto duration        = Clock::now() - timepoint;

    auto qoipp_desc = qoipp::Desc{
        .width      = desc.width,
        .height     = desc.height,
        .channels   = qoipp::to_channels(desc.channels).value(),    // always RGB or RGBA
        .colorspace = qoipp::to_colorspace(static_cast<std::uint8_t>(desc.colorspace)).value(),
    };

    return {
        .image = { encoded, qoipp_desc },
        .time  = duration,
    };
}

EncodeResult<> qoipp_encode(const RawImage& image)
{
    auto timepoint = Clock::now();

    auto desc   = image.desc;
    auto worst  = desc.width * desc.height * (static_cast<std::size_t>(desc.channels) + 1) + 14 + 8;
    auto buffer = qoipp::ByteVec(worst);
    auto count  = qoipp::encode_into(buffer, image.data, image.desc).value();
    buffer.resize(count);

    auto duration = Clock::now() - timepoint;

    return {
        .image = { buffer, image.desc },
        .time  = duration,
    };
}

DecodeResult qoipp_decode(const QoiImage& image)
{
    auto timepoint       = Clock::now();
    auto [decoded, desc] = qoipp::decode(image.data).value();
    auto duration        = Clock::now() - timepoint;

    return {
        .image = { decoded, desc },
        .time  = duration,
    };
}

EncodeResult<PngImage> stb_encode(const RawImage& image)
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

    auto result = EncodeResult<PngImage>{
        .image = {
            .data = { encoded, encoded + len },
            .desc = desc,
        },
        .time = duration,
    };

    stbi_image_free(encoded);
    return result;
}

DecodeResult stb_decode(const PngImage& image)
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

    auto channels_real = qoipp::to_channels(channels);
    if (not channels_real) {
        throw std::runtime_error{ fmt::format("Number of channels ({}) is not supported (stb)", channels) };
    }

    auto result = DecodeResult{
        .image = {
            .data = { data, data + size },
            .desc = {
                .width      = static_cast<unsigned int>(width),
                .height     = static_cast<unsigned int>(height),
                .channels   = *channels_real,
                .colorspace = qoipp::Colorspace::sRGB,
            },
        },
        .time = duration,
    };

    stbi_image_free(data);
    return result;
}

EncodeResult<PngImage> fpng_encode(const RawImage& image)
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

    return {
        .image = {
            .data = std::move(encoded),
            .desc = desc,
        },
        .time = duration,
    };
}

DecodeResult fpng_decode(const PngImage& image)
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
    auto channels_real = qoipp::to_channels(channels);
    if (not channels_real) {
        throw std::runtime_error{ fmt::format("Number of channels ({}) is not supported (fpng)", channels) };
    }

    return {
        .image = {
            .data = std::move(decoded),
            .desc = {
                .width      = width,
                .height     = height,
                .channels   = *channels_real,
                .colorspace = qoipp::Colorspace::sRGB,
            },
        },
        .time = duration,
    };
}

BenchmarkResult benchmark(const RawImage& raw_image, const fs::path& file, const Options& opt)
{
    fmt::println("\t>> Benchmarking '{}'", file);

    auto qoi_image = qoi_encode_wrapper(raw_image).image;

    if (opt.verify) {
        const auto verify = [&]<typename T>(const T& left_image, const T& right_image)
            requires (std::same_as<T, RawImage> or std::same_as<T, QoiImage>)
        {
            if (left_image.data != right_image.data || left_image.desc != right_image.desc) {
                fmt::println("\t\tVerification failed for {} [skipped]", file);
                return false;
            }
            return true;
        };

        {    // qoi encode -> qoipp decode -> compare with raw_image
            fmt::println("\t\tverifying qoi   encode -> qoipp decode");
            auto [qoi_encoded, _]    = qoi_encode_wrapper(raw_image);
            auto [qoipp_decoded, __] = qoipp_decode(qoi_encoded);
            if (!verify(qoipp_decoded, raw_image)) {
                return { .file = file };
            }
        }

        {    // qoipp encode -> qoi decode -> compare with raw_image
            fmt::println("\t\tverifying qoipp encode -> qoi   decode");
            auto [qoipp_encoded, _] = qoipp_encode(raw_image);
            auto [qoi_decoded, __]  = qoi_decode_wrapper(qoipp_encoded);
            if (!verify(qoi_decoded, raw_image)) {
                return { .file = file };
            }
        }

        {    // qoi decode -> qoipp encode -> compare with qoi_image
            fmt::println("\t\tverifying qoi   decode -> qoipp encode");
            auto [qoi_decoded, _]    = qoi_decode_wrapper(qoi_image);
            auto [qoipp_encoded, __] = qoipp_encode(qoi_decoded);
            if (!verify(qoipp_encoded, qoi_image)) {
                return { .file = file };
            }
        }

        {    // qoipp decode -> qoi encode -> compare with qoi_image
            fmt::println("\t\tverifying qoipp decode -> qoi   encode");
            auto [qoipp_decoded, _] = qoipp_decode(qoi_image);
            auto [qoi_encoded, __]  = qoi_encode_wrapper(qoipp_decoded);
            if (!verify(qoi_encoded, qoi_image)) {
                return { .file = file };
            }
        }
    }

    auto benchmark_impl = [&](auto func, const auto& image) {
        auto [_, time] = func(image);
        if (opt.warmup) {
            for (auto i = 0; i < 3; ++i) {
                func(image);
            }
        }

        auto        total = Duration::zero();
        std::size_t size  = 0;
        for (auto run = opt.runs; run-- > 0;) {
            auto [coded, time]  = func(image);
            total              += time;
            size                = coded.data.size();
        }

        return std::make_pair(total / opt.runs, size);
    };

    // the benchmark starts here

    auto result = BenchmarkResult{
        .desc     = qoi_image.desc,
        .file     = file,
        .raw_size = raw_image.data.size(),
    };

    fmt::println("\t\tbenchmark");

    if (opt.encode) {
        // clang-format off
        if (opt.qoi) {
            auto [qoi_time, qoi_size] = benchmark_impl(qoi_encode_wrapper, raw_image);
            result.libs_info[lib::Lib::qoi].encode_time  = qoi_time;
            result.libs_info[lib::Lib::qoi].encoded_size = qoi_size;
        }
        if (opt.qoixx) {
            auto [qoixx_time, qoixx_size] = benchmark_impl(qoixx_encode, raw_image);
            result.libs_info[lib::Lib::qoixx].encode_time  = qoixx_time;
            result.libs_info[lib::Lib::qoixx].encoded_size = qoixx_size;
        }
        if (opt.qoipp) {
            auto [qoipp_time, qoipp_size] = benchmark_impl(qoipp_encode, raw_image);
            result.libs_info[lib::Lib::qoipp].encode_time  = qoipp_time;
            result.libs_info[lib::Lib::qoipp].encoded_size = qoipp_size;
        }
        if (opt.stb) {
            auto [stb_time, stb_size] = benchmark_impl(stb_encode, raw_image);
            result.libs_info[lib::Lib::stb].encode_time  = stb_time;
            result.libs_info[lib::Lib::stb].encoded_size = stb_size;
        }
        if (opt.fpng) {
            auto [fpng_time, fpng_size] = benchmark_impl(fpng_encode, raw_image);
            result.libs_info[lib::Lib::fpng].encode_time  = fpng_time;
            result.libs_info[lib::Lib::fpng].encoded_size = fpng_size;
        }
        // clang-format on
    }

    if (opt.decode) {
        // clang-format off
        if (opt.qoi) {
            auto [qoi_time, _] = benchmark_impl(qoi_decode_wrapper, qoi_image);
            result.libs_info[lib::Lib::qoi].decode_time = qoi_time;
        }
        if (opt.qoixx) {
            auto [qoixx_time, _] = benchmark_impl(qoixx_decode, qoi_image);
            result.libs_info[lib::Lib::qoixx].decode_time = qoixx_time;
        }
        if (opt.qoipp) {
            auto [qoipp_time, _] = benchmark_impl(qoipp_decode, qoi_image);
            result.libs_info[lib::Lib::qoipp].decode_time = qoipp_time;
        }
        if (opt.stb) {
            auto png_image_stb = stb_encode(raw_image).image;
            auto [stb_time, _] = benchmark_impl(stb_decode, png_image_stb);
            result.libs_info[lib::Lib::stb].decode_time = stb_time;
        }
        if (opt.fpng) {
            auto png_image_fpng = fpng_encode(raw_image).image ;
            auto [fpng_time, _] = benchmark_impl(fpng_decode, png_image_fpng);
            result.libs_info[lib::Lib::fpng].decode_time = fpng_time;
        }
        // clang-format on
    }

    return result;
}

std::vector<BenchmarkResult> benchmark_directory(const fs::path& path, const Options& opt)
{
    auto results = std::vector<BenchmarkResult>{};

    auto benchmark_file = [&](const fs::path& file) {
        try {
            auto raw_image = load_image(file);
            auto result    = benchmark(raw_image, file, opt);
            result.print(opt.color);
            results.push_back(result);
        } catch (const std::exception& e) {
            fmt::println("\t\tBenchmarking failed for '{}' (exception): {}", file, e.what());
            fmt::println("\t\tSkipping file '{}'", file);
        }
    };

    if (opt.recurse) {
        fmt::println(">> Benchmarking {} (recurse)...", path / "**/*.png");

        for (const auto& path : fs::recursive_directory_iterator{ path }) {
            if (fs::is_regular_file(path) && path.path().extension() == ".png") {
                benchmark_file(path.path());
            }
        }
    } else {
        fmt::println(">> Benchmarking {}...", path / "*.png");

        for (const auto& path : fs::directory_iterator{ path }) {
            if (fs::is_regular_file(path) && path.path().extension() == ".png") {
                benchmark_file(path.path());
            }
        }
    }

    fmt::println("\t>> Benchmarking '{}' done!", path);

    return results;
}

BenchmarkResult average_results(std::span<const BenchmarkResult> results)
{
    auto average = BenchmarkResult{
        .desc = {
            .width      = 0,
            .height     = 0,
            .channels   = {},
            .colorspace = {},
        },
        .file    = "Summary",
        .raw_size = 0,
    };

    auto add = [&](const std::map<lib::Lib, BenchmarkResult::LibInfo>& values) {
        for (auto&& [k, v] : values) {
            average.libs_info[k].encode_time  += v.encode_time;
            average.libs_info[k].decode_time  += v.decode_time;
            average.libs_info[k].encoded_size += v.encoded_size;
        }
    };

    for (auto&& result : results) {
        average.raw_size += result.raw_size;
        add(result.libs_info);
    }

    for (auto& [_, v] : average.libs_info) {
        v.encode_time /= static_cast<long>(results.size());
        v.decode_time /= static_cast<long>(results.size());
    }

    return average;
}

int main(int argc, char* argv[])
try {
    auto app     = CLI::App{ "Qoibench - Benchmarking tool for QOI" };
    auto opt     = Options{};
    auto dirpath = fs::path{};

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
        auto results = benchmark_directory(dirpath, opt);
        auto summary = average_results(results);
        summary.print(opt.color);
    } else if (fs::is_regular_file(dirpath) and dirpath.extension() == ".png") {
        try {
            auto raw_image = load_image(dirpath);
            auto result    = benchmark(raw_image, dirpath, opt);
            result.print(opt.color);
        } catch (const std::exception& e) {
            fmt::println("\t>> Benchmarking failed for '{}' (exception): {}", dirpath, e.what());
            return 1;
        }
    } else {
        fmt::println("'{}' is not a directory nor a png file", dirpath);
        return 1;
    }

} catch (std::exception& e) {
    fmt::println("Exception occurred: {}", e.what());
    return 1;
}
