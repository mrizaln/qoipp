#include <qoipp/qoipp.hpp>

#include <cstring>

using qoipp::Span;

// TODO: fuzz encode function too

// to build:
//   clang -fsanitize=address,fuzzer -std=c++20 -g -O1 -I <include> source/qoifuzz.cpp <libqoipp.a> -o qoifuzz

constexpr auto max_size = 256u * 1024 * 1024;    // 512 MiB

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    auto buffer = qoipp::Vec(max_size);

    auto header = qoipp::read_header({ data, size });
    if (header) {
        auto [w, h, c, _] = *header;
        if (static_cast<size_t>(c) * w * h <= max_size) {
            qoipp::decode({ data, size });
            qoipp::decode_into(buffer, { data, size });
        }
    }

    constexpr auto desc_size = sizeof(qoipp::Desc);
    if (size > desc_size) {
        // the first few bytes will be used as desc.
        auto arr = std::array<std::byte, desc_size>{};
        std::memcpy(arr.data(), data, desc_size);

        const auto desc = std::bit_cast<qoipp::Desc>(arr);
        const auto size = desc.width * desc.height * static_cast<unsigned int>(desc.channels);
        if (size >= max_size) {
            return 0;
        }

        auto data_span = qoipp::CSpan{ data + desc_size, size - desc_size };

        qoipp::encode(data_span, desc);
        qoipp::encode_into(buffer, data_span, desc);
    }

    return 0;
}
