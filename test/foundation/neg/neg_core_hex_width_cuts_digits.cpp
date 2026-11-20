// The width of a Hex holds each value of the type of its value.  Eight
// digits hold 32 bits, so a Hex<8> of a 64-bit value would cut the high
// digits, and the deleted constructor refuses it at the call.

#include <foundation/core/Report.h>

#include <cstdint>

int main() {
    std::uint64_t const hash = 0x0123456789ABCDEFULL;
    ::foundation::core::report(::foundation::core::Sink::Out, "the hash is {}\n", ::foundation::core::Hex<8>{hash});
}
