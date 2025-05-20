#include "util.hpp"

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

    for (auto i = 5u; i < 1024; ++i) {
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

                auto [read, write]  = res.value();
                off                += read;
                encoded.insert(encoded.end(), out.begin(), out.begin() + write);
            }

            auto size       = encoded.size();
            auto additional = qoipp::constants::end_marker_size + encoder.has_run_count();
            encoded.resize(size + additional);

            encoder.finalize({ encoded.data() + size, additional });

            expect(that % qoi.size() == encoded.size());
            expect(rr::equal(qoi, encoded)) << util::compare_detail(qoi, encoded, 32);
        } | simple_cases;
    }
}
