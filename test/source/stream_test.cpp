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

using namespace ut::literals;
using namespace ut::operators;

using ut::expect;
using ut::fatal;
using ut::test;
using ut::that;

using util::lazy_compare;

struct TestCase
{
    qoipp::Desc desc;
    ByteCSpan   raw;
    ByteCSpan   qoi;
    ByteCSpan   qoi_incomplete;
};

int to_int(qoipp::Channels chan)
{
    return static_cast<int>(chan);
}

// wrapper
// -------
ByteVec encode(
    qoipp::StreamEncoder& encoder,
    qoipp::Desc           desc,
    ByteSpan              out_buffer,
    ByteCSpan             input,
    std::source_location  loc = std::source_location::current()
)
{
    expect(not encoder.is_initialized()) << "encoder shouldn't be double initialized" << fatal;

    auto encoded = ByteVec(qoipp::constants::header_size);

    auto res = encoder.initialize(encoded, desc);
    expect(res.has_value(), loc) << "write header should successful" << fatal;
    expect(that % res.value() == qoipp::constants::header_size);

    auto off = 0ul;
    auto out = out_buffer;

    while (off < input.size()) {
        auto in  = ByteCSpan{ input.data() + off, std::min(out.size(), input.size() - off) };
        auto res = encoder.encode(out, in);
        expect(res.has_value(), loc) << "encode should successful" << fatal;

        off += res->processed;
        encoded.insert(encoded.end(), out.begin(), out.begin() + static_cast<long>(res->written));
    }

    auto size       = encoded.size();
    auto additional = qoipp::constants::end_marker_size + encoder.has_run_count();
    encoded.resize(size + additional);

    auto fin_res = encoder.finalize({ encoded.data() + size, additional });
    expect(fin_res.has_value(), loc) << "finalization should successful" << fatal;
    expect(fin_res.value() == additional, loc) << "written at finalization should be the same" << fatal;

    return encoded;
}

ByteVec decode(
    qoipp::StreamDecoder&          decoder,
    qoipp::Desc                    ref_desc,
    ByteSpan                       out_buffer,
    ByteCSpan                      input,
    std::optional<qoipp::Channels> target = std::nullopt,
    std::source_location           loc    = std::source_location::current()
)
{
    expect(not decoder.is_initialized()) << "decoder shouldn't be double initialized" << fatal;

    auto decoded = ByteVec{};

    if (target) {
        ref_desc.channels = *target;
    }

    auto parsed_desc = decoder.initialize({ input.data(), qoipp::constants::header_size }, target).value();
    expect(that % ref_desc == parsed_desc, loc) << "decoded desc should match the reference";

    auto off = qoipp::constants::header_size;
    auto out = out_buffer;

    auto end = input.size() - qoipp::constants::end_marker_size;

    while (off < end) {
        auto in  = ByteCSpan{ input.data() + off, std::min(out.size(), end - off) };
        auto res = decoder.decode(out, in);
        expect(res.has_value(), loc) << "decode should be successful" << fatal;

        off += res->processed;
        decoded.insert(decoded.end(), out.begin(), out.begin() + static_cast<long>(res->written));
    }

    while (decoder.has_run_count()) {
        auto count = decoder.drain_run(out).value();
        decoded.insert(decoded.end(), out.begin(), out.begin() + static_cast<long>(count));
    }

    decoder.reset();    // just good hygiene
    return decoded;
}
// -------

int main()
{
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

    const auto test_case_3 = TestCase{
        .desc           = desc_3,
        .raw            = raw_image_3,
        .qoi            = qoi_image_3,
        .qoi_incomplete = qoi_image_incomplete_3,
    };
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

    const auto test_case_4 = TestCase{
        .desc           = desc_4,
        .raw            = raw_image_4,
        .qoi            = qoi_image_4,
        .qoi_incomplete = qoi_image_incomplete_4,
    };
    // ----

    const auto simple_cases = std::array{ test_case_3, test_case_4 };

    // only one encoder and decoder to also test the reusability :D
    auto encoder = qoipp::StreamEncoder{};
    auto decoder = qoipp::StreamDecoder{};

    for (auto i = 5u; i <= 1024; ++i) {
        test("stream encoder encode original | " + std::to_string(i)) = [&](TestCase input) {
            const auto& [desc, raw, qoi, _] = input;

            auto buffer  = ByteVec(i);
            auto encoded = encode(encoder, desc, buffer, raw);

            expect(that % qoi.size() == encoded.size());
            expect(rr::equal(qoi, encoded)) << lazy_compare(qoi, encoded, 1, 32);
        } | simple_cases;
    }

    for (auto i = 5u; i <= 1024; ++i) {
        test("stream decoder decode original | " + std::to_string(i)) = [&](TestCase input) {
            const auto& [desc, raw, qoi, _] = input;

            auto buffer  = ByteVec(i);
            auto decoded = decode(decoder, desc, buffer, qoi);

            expect(that % raw.size() == decoded.size());
            expect(rr::equal(raw, decoded)) << lazy_compare(raw, decoded, to_int(desc.channels), 8);
        } | simple_cases;

        test("stream decoder decode to RGB | " + std::to_string(i)) = [&](TestCase input) {
            const auto& [desc, raw, qoi, _] = input;

            auto rgb_raw = desc.channels == qoipp::Channels::RGB ? ByteVec{ raw.begin(), raw.end() }
                                                                 : util::to_rgb(raw);

            auto buffer  = ByteVec(i);
            auto decoded = decode(decoder, desc, buffer, qoi, qoipp::Channels::RGB);

            expect(that % rgb_raw.size() == decoded.size());
            expect(rr::equal(rgb_raw, decoded)) << lazy_compare(rgb_raw, decoded, to_int(desc.channels), 8);
        } | simple_cases;

        test("stream decoder decode to RGBA | " + std::to_string(i)) = [&](TestCase input) {
            const auto& [desc, raw, qoi, _] = input;

            auto rgba_raw = desc.channels == qoipp::Channels::RGBA    //
                              ? ByteVec{ raw.begin(), raw.end() }
                              : util::to_rgba(raw);

            auto buffer  = ByteVec(i);
            auto decoded = decode(decoder, desc, buffer, qoi, qoipp::Channels::RGBA);

            expect(that % rgba_raw.size() == decoded.size());
            expect(rr::equal(rgba_raw, decoded)) << lazy_compare(rgba_raw, decoded, to_int(desc.channels), 8);
        } | simple_cases;

        test("stream decoder decode incomplete (still work) | " + std::to_string(i)) = [&](TestCase input) {
            const auto& [desc, raw, qoi, qoi_incomplete] = input;

            auto buffer     = ByteVec(i);
            auto decoded    = decode(decoder, desc, buffer, qoi_incomplete);
            auto incomplete = raw.subspan(0, decoded.size());

            expect(that % raw.size() != decoded.size());
            expect(rr::equal(incomplete, decoded)) << lazy_compare(raw, decoded, to_int(desc.channels), 8);
        } | simple_cases;
    }

    fmt::println("Testing on real images...");

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

                    // randomize :D
                    auto range       = std::uniform_int_distribution{ 5ul, raw.size() - 1 };
                    auto buffer      = ByteVec(range(rng));
                    auto qoipp_image = encode(encoder, desc, buffer, raw);

                    expect(that % qoi_image.data.size() == qoipp_image.size());
                    expect(that % qoi_image.desc == image.desc);
                    expect(rr::equal(qoi_image.data, qoipp_image))
                        << lazy_compare(qoi_image.data, qoipp_image, 1, 32);
                };
            }
        }

        if (path.extension() == ".qoi") {
            fmt::println("Testing decode on '{}'", path.filename());

            for (auto i : rv::iota(0, 3)) {
                test(fmt::format("qoi images round trip test {} {}", path.filename().string(), i)) = [&] {
                    auto qoi = util::read_file(path);

                    const auto [raw_image_ref, desc_ref] = util::qoi_decode(qoi);

                    // randomize :D
                    auto end       = qoi.size() - qoipp::constants::end_marker_size;
                    auto range     = std::uniform_int_distribution{ 5ul, end - 1 };
                    auto buffer    = ByteVec(range(rng));
                    auto raw_image = decode(decoder, desc_ref, buffer, qoi);

                    expect(that % raw_image_ref.size() == raw_image.size());
                    expect(rr::equal(raw_image_ref, raw_image))
                        << lazy_compare(raw_image_ref, raw_image, to_int(desc_ref.channels), 8);
                };
            }
        }
    }
}
