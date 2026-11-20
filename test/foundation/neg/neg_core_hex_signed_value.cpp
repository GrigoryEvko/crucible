// A Hex takes an unsigned integer.  The digits of a signed value depend on
// the width of its type, so one set of bits has two texts, and the deleted
// constructor refuses it at the call.

#include <foundation/core/Report.h>

#include <cstdint>

int main() {
    std::int64_t const delta = -1;
    ::foundation::core::report(::foundation::core::Sink::Out, "the delta is {}\n", ::foundation::core::Hex<16>{delta});
}
