#include "util.hpp"

#include <qoipp/stream.hpp>
#include <random>

using qoipp::Byte;
using qoipp::ByteArr;
using qoipp::ByteCSpan;
using qoipp::ByteSpan;
using qoipp::ByteVec;

namespace fs = std::filesystem;
namespace ut = boost::ut;
namespace rr = ranges;
namespace rv = ranges::views;

int main()
{
    using namespace ut::literals;
    using namespace ut::operators;
    using ut::expect, ut::test, ut::that;

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

    for (auto i = 5u; i <= 1024; ++i) {
        test("stream encoder should be able to encode image | " + std::to_string(i)) = [&](auto&& input) {
            const auto& [desc, raw, qoi, _] = input;

            fmt::println("encode start chan={} buf={}", static_cast<int>(desc.channels), i);

            auto encoder = qoipp::StreamEncoder{};
            auto encoded = ByteVec(qoipp::constants::header_size);

            auto res = encoder.initialize(encoded, desc);
            expect(res.has_value()) << "write header should successful";

            auto off = 0ul;
            auto out = ByteVec(i);

            while (off < raw.size()) {
                auto in  = ByteCSpan{ raw.data() + off, std::min(out.size(), raw.size() - off) };
                auto res = encoder.encode(out, in);
                expect(res.has_value()) << "encode should successful";

                off += res->processed;
                encoded.insert(encoded.end(), out.begin(), out.begin() + res->written);
            }

            auto size       = encoded.size();
            auto additional = qoipp::constants::end_marker_size + encoder.has_run_count();
            encoded.resize(size + additional);

            encoder.finalize({ encoded.data() + size, additional });

            expect(that % qoi.size() == encoded.size());
            expect(rr::equal(qoi, encoded)) << util::lazy_compare(qoi, encoded, 1, 32);
        } | simple_cases;
    }

    for (auto i = 5u; i <= 1024; ++i) {
        test("stream decoder should be able to decode image | " + std::to_string(i)) = [&](auto&& input) {
            const auto& [desc, raw, qoi, _] = input;

            fmt::println("decode start chan={} | buf={}", static_cast<int>(desc.channels), i);

            auto decoder = qoipp::StreamDecoder{};
            auto decoded = ByteVec{};

            auto parsed_desc = decoder.initialize({ qoi.data(), qoipp::constants::header_size }).value();
            expect(desc == parsed_desc);

            auto off = qoipp::constants::header_size;
            auto out = ByteVec(i);

            auto end = qoi.size() - qoipp::constants::end_marker_size;

            while (off < end) {
                auto in  = ByteCSpan{ qoi.data() + off, std::min(out.size(), end - off) };
                auto res = decoder.decode(out, in);
                expect(res.has_value()) << "decode should successful";

                off += res->processed;
                decoded.insert(decoded.end(), out.begin(), out.begin() + res->written);
            }

            while (decoder.has_run_count()) {
                auto count = decoder.drain_run(out).value();
                decoded.insert(decoded.end(), out.begin(), out.begin() + count);
            }

            decoder.reset();

            expect(that % raw.size() == decoded.size());
            expect(rr::equal(raw, decoded))
                << util::lazy_compare(raw, decoded, static_cast<int>(desc.channels), 8);
        } | simple_cases;
    }

    fmt::print("Testing on real images...\n");

    if (!fs::exists(util::test_image_dir)) {
        fmt::println("Test image directory '{}' does not exist, skipping test", util::test_image_dir);
        fmt::println("Use the `fetch_test_images.sh` script to download the test images");
        return 0;
    }

    auto rng = std::mt19937{ std::random_device{}() };

    for (auto entry : fs::directory_iterator{ util::test_image_dir }) {
        const auto path = entry.path();

        if (path.extension() == ".png") {
            fmt::println("Testing encode on '{}'", path.filename());

            for (auto i : rv::iota(0, 3)) {
                test(fmt::format("png images round trip test {} {}", path.filename().string(), i)) = [&] {
                    const auto image     = util::load_image_stb(path);
                    const auto qoi_image = util::qoi_encode(image);

                    const auto& [raw, desc] = image;

                    auto encoder     = qoipp::StreamEncoder{};
                    auto qoipp_image = ByteVec(qoipp::constants::header_size);

                    auto res = encoder.initialize(qoipp_image, desc);
                    expect(res.has_value()) << "write header should successful";

                    auto off = 0ul;

                    // randomize :D
                    auto range = std::uniform_int_distribution{ 5ul, raw.size() - 1 };
                    auto out   = ByteVec(range(rng));

                    while (off < raw.size()) {
                        auto in  = ByteCSpan{ raw.data() + off, std::min(out.size(), raw.size() - off) };
                        auto res = encoder.encode(out, in);
                        expect(res.has_value()) << "encode should successful";

                        off += res->processed;
                        qoipp_image.insert(qoipp_image.end(), out.begin(), out.begin() + res->written);
                    }

                    auto size       = qoipp_image.size();
                    auto additional = qoipp::constants::end_marker_size + encoder.has_run_count();
                    qoipp_image.resize(size + additional);

                    encoder.finalize({ qoipp_image.data() + size, additional });

                    expect(that % qoi_image.data.size() == qoipp_image.size());
                    expect(qoi_image.desc == image.desc);
                    expect(rr::equal(qoi_image.data, qoipp_image))
                        << util::lazy_compare(qoi_image.data, qoipp_image, 32, 1);
                };
            }
        }

        if (path.extension() == ".qoi") {
            fmt::println("Testing decode on '{}'", path.filename());

            for (auto i : rv::iota(0, 3)) {
                test(fmt::format("qoi images round trip test {} {}", path.filename().string(), i)) = [&] {
                    auto qoi = util::read_file(path);

                    const auto [raw_image_ref, desc_ref] = util::qoi_decode(qoi);

                    auto decoder   = qoipp::StreamDecoder{};
                    auto raw_image = ByteVec{};

                    auto desc = decoder.initialize({ qoi.data(), qoipp::constants::header_size }).value();
                    expect(desc_ref == desc);

                    auto end = qoi.size() - qoipp::constants::end_marker_size;
                    auto off = qoipp::constants::header_size;

                    // randomize :D
                    auto range = std::uniform_int_distribution{ 5ul, end - 1 };
                    auto out   = ByteVec(range(rng));

                    while (off < end) {
                        auto in  = ByteCSpan{ qoi.data() + off, std::min(out.size(), end - off) };
                        auto res = decoder.decode(out, in);
                        expect(res.has_value()) << "decode should successful";

                        off += res->processed;
                        raw_image.insert(raw_image.end(), out.begin(), out.begin() + res->written);
                    }

                    while (decoder.has_run_count()) {
                        auto count = decoder.drain_run(out).value();
                        raw_image.insert(raw_image.end(), out.begin(), out.begin() + count);
                    }

                    decoder.reset();

                    expect(that % raw_image_ref.size() == raw_image.size());
                    expect(rr::equal(raw_image_ref, raw_image))
                        << util::lazy_compare(raw_image_ref, raw_image, 32, 1);
                };
            }
        }
    }
}
