// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-116 fixture #4: wrap() is constrained to byte-contiguous payloads.
// Arbitrary structs must define an explicit wire encoding first.

#include <crucible/cntp/Integrity.h>

#include <cstdint>

struct NotWireBytes {
    std::uint64_t value = 0;
};

int main() {
    (void)crucible::cntp::wrap(NotWireBytes{});
    return 0;
}
