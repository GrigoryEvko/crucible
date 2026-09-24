// A clock reading was trivially copyable, so std::bit_cast stamped any
// integer with a clock source and no constructor ran.  The copy and move
// constructors are user-provided now, so the reading is not trivially
// copyable, and bit_cast refuses it at its constraint.

#include <fixy/os/ClockSource.h>

#include <bit>
#include <cstdint>

int main() {
    auto forged = std::bit_cast<fixy::BootClockBytes<std::uint64_t>>(std::uint64_t{42});
    (void)forged;
    return 0;
}
