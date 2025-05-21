#include "util.hpp"

#include <qoipp/simple.hpp>

#if defined(__cpp_lib_expected)
static_assert(std::same_as<qoipp::Result<int>, std::expected<int, qoipp::Error>>);
#endif

using qoipp::Byte;
using qoipp::ByteCSpan;
using qoipp::ByteVec;
using qoipp::Image;

namespace fs = std::filesystem;
namespace ut = boost::ut;
namespace rr = ranges;
namespace rv = ranges::views;

int main()
{
    using namespace ut::literals;
    using namespace ut::operators;
    using ut::expect, ut::test, ut::that;

    // this number is chosen seems it happen both test images have this as a valid chunk boundary
    constexpr auto chunk_boundary = 1007u;

    // rgb
    // ---
    constexpr auto desc_3 = qoipp::Desc{
        .width      = 29,
        .height     = 17,
        .channels   = qoipp::Channels::RGB,
        .colorspace = qoipp::Colorspace::sRGB,
    };

    const auto raw_image_3 = ByteVec{
#include "image_raw_3.txt"
    };

    const auto qoi_image_3 = ByteVec{
#include "image_qoi_3.txt"
    };

    const auto qoi_image_incomplete_3 = ByteVec{
#include "image_qoi_3_incomplete.txt"
    };

    const auto test_case_3 = std::tie(desc_3, raw_image_3, qoi_image_3, qoi_image_incomplete_3);
    // ---

    // rgba
    // ----
    constexpr auto desc_4 = qoipp::Desc{
        .width      = 24,
        .height     = 14,
        .channels   = qoipp::Channels::RGBA,
        .colorspace = qoipp::Colorspace::sRGB,
    };

    const auto raw_image_4 = ByteVec{
#include "image_raw_4.txt"
    };

    const auto qoi_image_4 = ByteVec{
#include "image_qoi_4.txt"
    };

    const auto qoi_image_incomplete_4 = ByteVec{
#include "image_qoi_4_incomplete.txt"
    };

    const auto test_case_4 = std::tie(desc_4, raw_image_4, qoi_image_4, qoi_image_incomplete_4);
    // ----

    const auto simple_cases = std::array{ test_case_3, test_case_4 };

    "simple image encode"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        const auto encoded = qoipp::encode(raw, desc).value();
        expect(that % encoded.size() == qoi.size());
        expect(rr::equal(encoded, qoi));
    } | simple_cases;

    "image encode into buffer from span"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        "with sufficient buffer"_test = [&] {
            auto buffer            = ByteVec(qoipp::worst_size(desc).value());
            auto [count, complete] = qoipp::encode_into(buffer, raw, desc).value();
            auto encoded           = ByteCSpan{ buffer.begin(), count };

            expect(complete) << "encoding should complete";
            expect(that % encoded.size() == qoi.size());
            expect(rr::equal(encoded, qoi));
        };

        "with insufficient buffer"_test = [&] {
            auto buffer            = ByteVec(chunk_boundary);
            auto [count, complete] = qoipp::encode_into(buffer, raw, desc).value();

            expect(not complete) << "encoding should not be complete";
            expect(that % count == chunk_boundary);
            expect(rr::equal(buffer, rv::take(qoi, chunk_boundary)))
                << "image should partially encoded\n"
                << util::lazy_compare(buffer, rv::take(qoi, chunk_boundary));
        };
    } | simple_cases;

    "image encode into buffer from function"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto gen = [&](std::size_t index) -> qoipp::Pixel {
            auto offset = index * static_cast<std::size_t>(desc.channels);
            return {
                raw[offset],
                raw[offset + 1],
                raw[offset + 2],
                desc.channels == qoipp::Channels::RGBA ? raw[offset + 3] : static_cast<Byte>(0xFF),
            };
        };

        "with sufficient buffer"_test = [&] {
            auto buffer            = ByteVec(qoipp::worst_size(desc).value());
            auto [count, complete] = qoipp::encode_into(buffer, gen, desc).value();
            auto encoded           = ByteCSpan{ buffer.begin(), count };

            expect(complete) << "encoding should complete";
            expect(that % encoded.size() == qoi.size());
            expect(rr::equal(encoded, qoi));
        };

        "with insufficient buffer"_test = [&] {
            auto buffer            = ByteVec(chunk_boundary);
            auto [count, complete] = qoipp::encode_into(buffer, gen, desc).value();

            expect(not complete) << "encoding should not be complete";
            expect(that % count == chunk_boundary);
            expect(rr::equal(buffer, rv::take(qoi, chunk_boundary)))
                << "image should partially encoded\n"
                << util::lazy_compare(buffer, rv::take(qoi, chunk_boundary));
        };
    } | simple_cases;

    "image encode into function from span"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto result = qoipp::ByteVec{};
        auto out    = [&](Byte byte) { result.push_back(byte); };
        auto res    = qoipp::encode_into(out, raw, desc).value();

        expect(that % res == qoi.size());
        expect(that % result.size() == qoi.size());
        expect(rr::equal(result, qoi));
    } | simple_cases;

    "image encode into function from function"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto gen = [&](std::size_t index) -> qoipp::Pixel {
            auto offset = index * static_cast<std::size_t>(desc.channels);
            return {
                raw[offset],
                raw[offset + 1],
                raw[offset + 2],
                desc.channels == qoipp::Channels::RGBA ? raw[offset + 3] : static_cast<Byte>(0xFF),
            };
        };

        auto result = qoipp::ByteVec{};
        auto out    = [&](Byte byte) { result.push_back(byte); };
        auto res    = qoipp::encode_into(out, gen, desc).value();

        expect(that % res == qoi.size());
        expect(that % result.size() == qoi.size());
        expect(rr::equal(result, qoi));
    } | simple_cases;

    "simple image decode"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        const auto [decoded, actualdesc] = qoipp::decode(qoi).value();
        expect(actualdesc == desc);
        expect(that % decoded.size() == raw.size());
        expect(rr::equal(decoded, raw)) << util::lazy_compare(raw, decoded);
    } | simple_cases;

    "simple image decode wants RGB"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto rgb_image = desc.channels == qoipp::Channels::RGB ? raw : util::to_rgb(raw);
        auto rgb_desc  = qoipp::Desc{ desc.width, desc.height, qoipp::Channels::RGB, desc.colorspace };

        const auto [decoded, actualdesc] = qoipp::decode(qoi, qoipp::Channels::RGB).value();
        expect(actualdesc == rgb_desc);
        expect(that % decoded.size() == rgb_image.size());
        expect(rr::equal(decoded, rgb_image)) << util::lazy_compare(rgb_image, decoded);
    } | simple_cases;

    "simple image decode wants RGBA"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto rgba_image = desc.channels == qoipp::Channels::RGBA ? raw : util::to_rgba(raw);
        auto rgba_desc  = qoipp::Desc{ desc.width, desc.height, qoipp::Channels::RGBA, desc.colorspace };

        const auto [decoded, actualdesc] = qoipp::decode(qoi, qoipp::Channels::RGBA).value();
        expect(actualdesc == rgba_desc);
        expect(that % decoded.size() == rgba_image.size());
        expect(rr::equal(decoded, rgba_image)) << util::lazy_compare(rgba_image, decoded);
    } | simple_cases;

    "image decode into buffer"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto size       = desc.width * desc.height * static_cast<std::size_t>(desc.channels);
        auto buffer     = ByteVec(size);
        auto actualdesc = qoipp::decode_into(buffer, qoi).value();
        auto decoded    = ByteCSpan{ buffer.begin(), size };

        expect(actualdesc == desc);
        expect(that % decoded.size() == raw.size());
        expect(rr::equal(decoded, raw)) << util::lazy_compare(raw, decoded);
    } | simple_cases;

    "image decode into function"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto result = qoipp::ByteVec{};
        auto out    = [&](qoipp::Pixel pixel) {
            auto arr = std::bit_cast<std::array<Byte, 4>>(pixel);
            if (desc.channels == qoipp::Channels::RGBA) {
                result.insert(result.end(), arr.begin(), arr.begin() + 4);
            } else {
                result.insert(result.end(), arr.begin(), arr.begin() + 3);
            }
        };
        auto actualdesc = qoipp::decode_into(out, qoi).value();

        expect(actualdesc == desc);
        expect(that % result.size() == raw.size());
        expect(rr::equal(result, raw)) << util::lazy_compare(raw, result);
    } | simple_cases;

    "image encode to and decode from file"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto qoifile = util::mktemp();

        expect(ut::nothrow([&] {
            const auto res = qoipp::encode_into(qoifile, raw, desc, false);
            expect(res.has_value());
        }));
        expect(ut::nothrow([&] {
            const auto res = qoipp::encode_into(qoifile, raw, desc, false);
            expect(not res.has_value() and res.error() == qoipp::Error::FileExists);
        }));

        qoipp::Image decoded;
        expect(ut::nothrow([&] { decoded = qoipp::decode(qoifile).value(); }));
        expect(decoded.desc == desc);
        expect(that % decoded.data.size() == raw.size());
        expect(rr::equal(decoded.data, raw)) << util::lazy_compare(raw, decoded.data);

        std::ofstream ofs{ qoifile, std::ios::trunc };
        expect(fs::is_empty(qoifile));

        expect(ut::nothrow([&] {
            const auto res = qoipp::decode(qoifile);
            expect(not res.has_value() and res.error() == qoipp::Error::Empty);
        })) << "Empty file should error with qoipp::Error::Empty";

        fs::remove(qoifile);

        expect(ut::nothrow([&] {
            const auto res = qoipp::decode(qoifile);
            expect(not res.has_value() and res.error() == qoipp::Error::FileNotExists);
        })) << "Non-existent file should error with qoipp::Error::FileNotExists";

        expect(!fs::exists(qoifile)) << "File should not be created if it previously not exist";
    } | simple_cases;

    "image header read"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        const auto header = qoipp::read_header(qoi);
        expect(header.has_value()) << "Invalid header";
        expect(*header == desc);

        const auto empty_header = qoipp::read_header(ByteVec{});
        expect(!empty_header.has_value());

        const auto invalid_header_str = ByteVec{ 0x00, 0x01, 0x02, 0x03 };
        const auto invalid_header     = qoipp::read_header(invalid_header_str);
        expect(!invalid_header.has_value());
    } | simple_cases;

    "image header read from file"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        const auto qoifile = util::mktemp();
        expect(ut::nothrow([&] { qoipp::encode_into(qoifile, raw, desc, false); }));

        const auto header = qoipp::read_header(qoifile);
        expect(header.has_value()) << "Invalid header";
        expect(*header == desc);

        std::ofstream ofs{ qoifile, std::ios::trunc };
        expect(fs::is_empty(qoifile));

        const auto empty_header = qoipp::read_header(qoifile);
        expect(!empty_header.has_value());

        fs::remove(qoifile);
    } | simple_cases;

    "image decode on incomplete data should still work"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, qoi_incomplete] = input;

        const auto [decoded, actualdesc] = qoipp::decode(qoi_incomplete).value();
        expect(actualdesc == desc);
        expect(that % decoded.size() == raw.size());
    } | simple_cases;

    fmt::print("Testing on real images...\n");

    if (!fs::exists(util::test_image_dir)) {
        fmt::println("Test image directory '{}' does not exist, skipping test", util::test_image_dir);
        fmt::println("Use the `fetch_test_images.sh` script to download the test images");
        return 0;
    }

    for (auto entry : fs::directory_iterator{ util::test_image_dir }) {
        const auto path = entry.path();

        if (path.extension() == ".png") {
            fmt::println("Testing encode on '{}'", path.filename());
            test("png images round trip test compared to reference" + path.filename().string()) = [&] {
                const auto image       = util::load_image_stb(path);
                const auto qoi_image   = util::qoi_encode(image);
                const auto qoipp_image = qoipp::encode(image.data, image.desc).value();

                expect(that % qoi_image.data.size() == qoipp_image.size());
                expect(qoi_image.desc == image.desc);
                expect(rr::equal(qoi_image.data, qoipp_image))
                    << util::lazy_compare(qoi_image.data, qoipp_image, 32, 1);
            };
        }
        if (path.extension() == ".qoi") {
            fmt::println("Testing encode on '{}'", path.filename());
            test("qoipp decode compared to reference" + path.filename().string()) = [&] {
                auto qoi_image = util::read_file(path);

                const auto [raw_image_ref, desc_ref] = util::qoi_decode(qoi_image);
                const auto [raw_image, desc]         = qoipp::decode(path).value();

                expect(that % raw_image_ref.size() == raw_image.size());
                expect(desc_ref == desc);
                expect(rr::equal(raw_image_ref, raw_image))
                    << util::lazy_compare(raw_image_ref, raw_image, 32, 1);
            };
        }
    }
}
