#pragma once

#include <qoipp/common.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#include <stb_image.h>

#include <boost/ut.hpp>
#include <dtl_modern/dtl_modern.hpp>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <range/v3/view.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace util
{
    namespace fs = std::filesystem;
    namespace rv = ranges::views;

    inline const auto test_image_dir = fs::current_path() / "resources" / "qoi_test_images";

    inline fs::path mktemp()
    {
        auto name = std::tmpnam(nullptr);
        return fs::temp_directory_path() / name;
    }

    inline qoipp::ByteVec read_file(const fs::path& path)
    {
        auto file = std::ifstream{ path, std::ios::binary };
        file.exceptions(std::ios::failbit | std::ios::badbit);
        auto data = qoipp::ByteVec(fs::file_size(path));
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return data;
    }

    inline qoipp::ByteVec to_rgb(qoipp::ByteCSpan data)
    {
        if (data.size() % 4) {
            throw std::invalid_argument("data size must be a multiple of 4");
        }

        auto result = qoipp::ByteVec{};
        result.reserve(data.size() / 4 * 3);

        for (auto chunk : rv::chunk(data, 4)) {
            result.insert(result.end(), chunk.begin(), chunk.begin() + 3);
        }

        return result;
    }

    inline qoipp::ByteVec to_rgba(qoipp::ByteCSpan data)
    {
        if (data.size() % 3) {
            throw std::invalid_argument("data size must be a multiple of 3");
        }

        auto result = qoipp::ByteVec{};
        result.reserve(data.size() / 3 * 4);

        for (auto chunk : rv::chunk(data, 3)) {
            result.insert(result.end(), chunk.begin(), chunk.begin() + 3);
            result.push_back(0xFF);
        }

        return result;
    }

    inline qoipp::Image load_image_stb(const fs::path& file)
    {
        int   width, height, channels;
        auto* data = stbi_load(file.c_str(), &width, &height, &channels, 0);
        if (data == nullptr) {
            throw std::runtime_error{ fmt::format("Error decoding file '{}' (stb)", file) };
        }

        auto image = qoipp::Image{
        .data = { data, data + width * height * channels},
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

    inline std::string compare(qoipp::ByteCSpan lhs, qoipp::ByteCSpan rhs, int chunk = 1, int num_chunks = 32)
    {
        auto to_span = [](auto&& r) { return qoipp::ByteCSpan{ r.begin(), r.end() }; };

        auto lchunked = lhs | rv::chunk(chunk) | rv::transform(to_span) | ranges::to<std::vector>();
        auto rchunked = rhs | rv::chunk(chunk) | rv::transform(to_span) | ranges::to<std::vector>();

        auto [lcs, ses, edit_dist] = dtl_modern::diff(lchunked, rchunked, [](auto l, auto r) {
            auto lz = l.size();
            auto rz = r.size();
            return lz != rz ? false : std::equal(l.begin(), l.end(), r.begin(), r.end());
        });

        auto buffer = std::string{};
        auto out    = std::back_inserter(buffer);

        const auto red   = bg(fmt::color::dark_red);
        const auto green = bg(fmt::color::dark_green);
        const auto gray  = fg(fmt::color::gray);

        fmt::format_to(out, "\n{:>4}  ", "");
        for (auto i : rv::iota(0, num_chunks)) {
            fmt::format_to(out, "{:>{}} ", i, chunk * 2);
        }

        for (auto count = 0; auto [elem, info] : ses.get()) {
            if (count % num_chunks == 0) {
                out = '\n';
                fmt::format_to(out, "{:>4}: ", count / num_chunks);
            }

            using E     = dtl_modern::SesEdit;
            auto joined = fmt::join(elem, "");
            switch (info.m_type) {
            case E::Common: fmt::format_to(out, gray, "{:02x} ", joined); break;
            case E::Delete: fmt::format_to(out, red, "{:02x} ", joined); break;
            case E::Add: fmt::format_to(out, green, "{:02x} ", joined); break;
            }

            ++count;
        }

        return buffer;
    }

    inline auto lazy_compare(qoipp::ByteCSpan lhs, qoipp::ByteCSpan rhs, int chunk = 1, int num_chunks = 32)
    {
        return [=] { return compare(lhs, rhs, chunk, num_chunks); };
    }
}
