#include <qoipp.hpp>

using qoipp::Span;

// TODO: fuzz encode function too

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    qoipp::decode({ data, size });
    return 0;
}
