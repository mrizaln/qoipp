#include <qoipp.hpp>

using qoipp::ByteSpan;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    auto bytes = reinterpret_cast<std::byte*>(const_cast<uint8_t*>(data));
    auto span  = ByteSpan{ bytes, size };

    try {
        auto [decoded, desc] = qoipp::decode(span);
    } catch (const std::exception& e) {
        /* ignore exception */
    }

    return 0;
}
