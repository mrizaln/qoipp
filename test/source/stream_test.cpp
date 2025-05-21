#include "util.hpp"

#include <qoipp/simple.hpp>
#include <qoipp/stream.hpp>

#include <thread>

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
    using ut::expect, ut::that;

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
    const auto do_encode    = false;

    for (auto i = 5u; do_encode and i < 1024; ++i) {
        "stream encoder should be able to encode image"_test = [&](auto&& input) {
            const auto& [desc, raw, qoi, _] = input;

            auto start = fmt::format("encode start chan={} buf={}", static_cast<int>(desc.channels), i);
            ut::log << start;
            fmt::println("{}", start);

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

    "stream decoder should be able to decode image"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto start = fmt::format(
            "decode start chan={} | buf={} | qoi={}", static_cast<int>(desc.channels), 256, qoi.size()
        );
        ut::log << start;
        fmt::println("{}", start);

        auto decoder = qoipp::StreamDecoder{};
        auto decoded = ByteVec{};

        auto parsed_desc = decoder.initialize({ qoi.data(), qoipp::constants::header_size }).value();
        expect(desc == parsed_desc);

        auto off = qoipp::constants::header_size;
        auto out = ByteArr<256>{};

        auto end = qoi.size() - qoipp::constants::end_marker_size;

        while (off < end) {
            auto in  = ByteCSpan{ qoi.data() + off, std::min(out.size(), end - off) };
            auto res = decoder.decode(out, in);
            expect(res.has_value()) << "decode should successful";

            fmt::println("read={} | write={} | off={} | size={}", res->processed, res->written, off, end);

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(50ms);

            off += res->processed;
            decoded.insert(decoded.end(), out.begin(), out.begin() + res->written);
        }

        while (decoder.has_run_count()) {
            auto count = decoder.drain(out).value();
            decoded.insert(decoded.end(), out.begin(), out.begin() + count);
        }

        decoder.reset();

        expect(that % raw.size() == decoded.size());
        expect(rr::equal(raw, decoded))
            << util::lazy_compare(raw, decoded, static_cast<int>(desc.channels), 8);
    } | simple_cases;
    // } | std::array{ test_case_4 };
    // } | std::array{ test_case_3 };
}
