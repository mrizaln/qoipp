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

    "stream encoder should be able to encode image"_test = [&](auto&& input) {
        const auto& [desc, raw, qoi, _] = input;

        auto encoder = qoipp::StreamEncoder{ desc.channels };
        auto encoded = ByteVec(qoipp::constants::header_size);

        auto res = qoipp::write_header(encoded, desc);
        expect(res.has_value()) << "write header should succesful";

        auto off = 0ul;
        auto out = ByteArr<256>{};

        fmt::println("encode start {} channels", static_cast<int>(desc.channels));

        while (off < raw.size()) {
            auto in             = ByteCSpan{ raw.data() + off, std::min(out.size(), raw.size() - off) };
            auto [read, write]  = encoder.encode(out, in);
            off                += read;
            if (read == 0 and write == 0) {
                break;
            }
            fmt::println("read: {} | write: {} | in: {}", read, write, in.size());
            encoded.insert(encoded.end(), out.begin(), out.begin() + write);

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(100ms);
        }

        auto size = encoded.size();
        encoded.resize(size + qoipp::constants::end_marker_size);

        qoipp::write_end_marker(ByteSpan{ encoded.begin() + size, qoipp::constants::end_marker_size });

        expect(that % encoded.size() == qoi.size());
        auto drop_header = rv::drop(qoipp::constants::header_size);
        expect(rr::equal(encoded, qoi)) << util::compare(drop_header(encoded), drop_header(qoi), 16);
    } | simple_cases;

    // auto [raw, desc] = util::load_image_stb("percival.png");

    // auto encoder = qoipp::StreamEncoder{ desc.channels };
    // auto encoded = ByteVec(qoipp::constants::header_size);

    // auto res = qoipp::write_header(encoded, desc);
    // expect(res.has_value()) << "write header should succesful";

    // auto off = 0ul;
    // auto out = ByteArr<256>{};

    // while (off < raw.size()) {
    //     auto in             = ByteCSpan{ raw.data() + off, std::min(out.size(), raw.size() - off) };
    //     auto [read, write]  = encoder.encode(out, in);
    //     off                += read;
    //     if (read == 0 and write == 0) {
    //         break;
    //     }
    //     fmt::println("read: {} | write: {} | in: {}", read, write, in.size());
    //     encoded.insert(encoded.end(), out.begin(), out.begin() + write);
    // }

    // auto size = encoded.size();
    // encoded.resize(size + qoipp::constants::end_marker_size);

    // qoipp::write_end_marker(ByteSpan{ encoded.begin() + size, qoipp::constants::end_marker_size });

    // auto ofile = std::ofstream{ "percival.qoi", std::ios::binary };
    // ofile.write(reinterpret_cast<char*>(encoded.data()), encoded.size());
}
