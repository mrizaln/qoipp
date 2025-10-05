#include <qoipp/simple.hpp>
#include <qoipp/stream.hpp>

#include <cstring>
#include <random>

// TODO: fuzz stream encoder/decoder too

// to build:
//   clang -fsanitize=address,fuzzer -std=c++20 -g -O1 -I <include> source/qoifuzz.cpp <libqoipp.a> -o qoifuzz

constexpr auto max_size    = 256u * 1024 * 1024;    // 512 MiB
constexpr auto header_size = qoipp::constants::header_size;

std::optional<qoipp::ByteVec> stream_encode(
    qoipp::StreamEncoder& encoder,
    qoipp::Desc           desc,
    qoipp::ByteSpan       out_buffer,
    qoipp::ByteCSpan      input
)
{
    auto encoded = qoipp::ByteVec(header_size);

    if (auto res = encoder.initialize(encoded, desc); not res) {
        return {};
    }

    auto off = 0ul;
    auto out = out_buffer;

    while (off < input.size()) {
        auto in  = qoipp::ByteCSpan{ input.data() + off, std::min(out.size(), input.size() - off) };
        auto res = encoder.encode(out, in);
        if (not res) {
            encoder.reset();
            return {};
        }

        off += res->processed;
        encoded.insert(encoded.end(), out.begin(), out.begin() + static_cast<long>(res->written));
    }

    auto size       = encoded.size();
    auto additional = qoipp::constants::end_marker_size + encoder.has_run_count();
    encoded.resize(size + additional);

    if (auto res = encoder.finalize({ encoded.data() + size, additional }); not res) {
        encoder.reset();
        return {};
    }

    encoder.reset();
    return encoded;
}

std::optional<qoipp::ByteVec> stream_decode(
    qoipp::StreamDecoder&          decoder,
    qoipp::ByteSpan                out_buffer,
    qoipp::ByteCSpan               input,
    std::optional<qoipp::Channels> target = std::nullopt
)
{
    auto decoded = qoipp::ByteVec{};

    auto parsed_desc = decoder.initialize({ input.data(), header_size }, target);
    if (not parsed_desc) {
        return {};
    }

    auto off = header_size;
    auto out = out_buffer;
    auto end = input.size() - qoipp::constants::end_marker_size;

    while (off < end) {
        auto in  = qoipp::ByteCSpan{ input.data() + off, std::min(out.size(), end - off) };
        auto res = decoder.decode(out, in);
        if (not res) {
            decoder.reset();
            return {};
        }

        off += res->processed;
        decoded.insert(decoded.end(), out.begin(), out.begin() + static_cast<long>(res->written));
    }

    while (decoder.has_run_count()) {
        auto count = decoder.drain_run(out).value();
        decoded.insert(decoded.end(), out.begin(), out.begin() + static_cast<long>(count));
    }

    decoder.reset();
    return decoded;
}

void fuzz_simple(const uint8_t* data, size_t size)
{
    auto span   = qoipp::ByteCSpan{ data, size };
    auto buffer = qoipp::ByteVec(max_size);

    auto header = qoipp::read_header(span);
    if (header) {
        auto [w, h, c, _] = *header;
        if (static_cast<size_t>(c) * w * h <= max_size) {
            std::ignore = qoipp::decode(span.subspan(header_size));
            std::ignore = qoipp::decode_into(buffer, span.subspan(header_size));
        }
    }

    constexpr auto desc_size = sizeof(qoipp::Desc);
    if (size > desc_size) {
        // the first few bytes will be used as desc.
        auto arr = std::array<std::byte, desc_size>{};
        std::memcpy(arr.data(), span.data(), desc_size);

        auto desc = std::bit_cast<qoipp::Desc>(arr);
        if (auto size = qoipp::count_bytes(desc); not size or *size >= max_size) {
            return;
        }

        std::ignore = qoipp::encode(span.subspan(desc_size), desc);
        std::ignore = qoipp::encode_into(buffer, span.subspan(desc_size), desc);
    }
}

void fuzz_stream(const uint8_t* data, size_t size)
{
    auto span   = qoipp::ByteCSpan{ data, size };
    auto buffer = qoipp::ByteVec(max_size);
    auto rng    = std::mt19937{ static_cast<std::mt19937::result_type>(std::time(nullptr)) };

    auto header = qoipp::read_header({ data, size });
    if (header) {
        auto [w, h, c, _] = *header;
        if (static_cast<size_t>(c) * w * h <= max_size) {
            auto range   = std::uniform_int_distribution{ header_size, buffer.size() };
            auto encoder = qoipp::StreamDecoder{};
            auto buf     = std::span{ buffer.data(), range(rng) };

            std::ignore = stream_decode(encoder, buf, span, qoipp::Channels::RGB);
            std::ignore = stream_decode(encoder, buf, span, qoipp::Channels::RGBA);
        }
    }

    constexpr auto desc_size = sizeof(qoipp::Desc);
    if (size > desc_size) {
        // the first few bytes will be used as desc.
        auto arr = std::array<std::byte, desc_size>{};
        std::memcpy(arr.data(), span.data(), desc_size);

        auto desc = std::bit_cast<qoipp::Desc>(arr);
        if (auto size = qoipp::count_bytes(desc); not size or *size >= max_size) {
            return;
        }

        auto range   = std::uniform_int_distribution{ header_size, buffer.size() };
        auto encoder = qoipp::StreamEncoder{};
        auto buf     = std::span{ buffer.data(), range(rng) };

        std::ignore = stream_encode(encoder, desc, buf, span.subspan(desc_size));
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    fuzz_simple(data, size);
    fuzz_stream(data, size);

    return 0;
}
