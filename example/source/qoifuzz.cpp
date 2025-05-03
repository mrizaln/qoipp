#include <qoipp.hpp>

using qoipp::Span;

// TODO: fuzz encode function too

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    try {
        auto [decoded, desc] = qoipp::decode({ data, size });
    } catch (const std::exception& e) {
        /* ignore exception */
    }

    return 0;
}
