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
    inline const auto test_image_dir = std::filesystem::current_path() / "resources" / "qoi_test_images";

    inline std::filesystem::path mktemp()
    {
        auto name = std::tmpnam(nullptr);
        return std::filesystem::temp_directory_path() / name;
    }

    inline qoipp::ByteVec read_file(const std::filesystem::path& path)
    {
        auto file = std::ifstream{ path, std::ios::binary };
        file.exceptions(std::ios::failbit | std::ios::badbit);
        auto data = qoipp::ByteVec(std::filesystem::file_size(path));
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

        for (auto chunk : ranges::views::chunk(data, 4)) {
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

        for (auto chunk : ranges::views::chunk(data, 3)) {
            result.insert(result.end(), chunk.begin(), chunk.begin() + 3);
            result.push_back(0xFF);
        }

        return result;
    }

    inline qoipp::Image load_image_stb(const std::filesystem::path& file)
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

    inline std::string compare(qoipp::ByteCSpan lhs, qoipp::ByteCSpan rhs, std::size_t chunk = 32)
    {
        auto to_span = [](auto&& r) { return qoipp::ByteCSpan{ r.begin(), r.end() }; };

        auto lchunked = lhs | ranges::views::chunk(chunk) | ranges::views::transform(to_span)
                      | ranges::to<std::vector>();
        auto rchunked = rhs | ranges::views::chunk(chunk) | ranges::views::transform(to_span)
                      | ranges::to<std::vector>();

        auto [lcs, ses, edit_dist]
            = dtl_modern::diff(lchunked, rchunked, [](qoipp::ByteCSpan l, qoipp::ByteCSpan r) {
                  auto lz = l.size();
                  auto rz = r.size();
                  return lz != rz ? false : std::equal(l.begin(), l.end(), r.begin(), r.end());
              });

        auto buffer = std::string{ '\n' };
        auto out    = std::back_inserter(buffer);

        const auto red   = fg(fmt::color::orange_red);
        const auto green = fg(fmt::color::green_yellow);

        auto prev_common = false;

        for (auto [elem, info] : ses.get()) {
            using E = dtl_modern::SesEdit;
            switch (info.m_type) {
            case E::Common: {
                if (not prev_common) {
                    prev_common = true;
                    fmt::format_to(out, "...\n");
                }
            } break;
            case E::Delete: {
                auto offset = elem.begin() - lhs.begin();
                prev_common = false;
                auto joined = fmt::join(elem, " ");
                fmt::format_to(out, red, "{:04x}-{:04x}: {:02x}\n", offset, offset + chunk, joined);
            } break;
            case E::Add: {
                auto offset = elem.begin() - rhs.begin();
                prev_common = false;
                auto joined = fmt::join(elem, " ");
                fmt::format_to(out, green, "{:04x}-{:04x}: {:02x}\n", offset, offset + chunk, joined);
            } break;
            }
        }

        return buffer;
    }
}
