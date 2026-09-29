// If a clock reading were trivially copyable, std::bit_cast would stamp
// any integer with a clock source and no constructor would run.  The
// copy and move constructors are user-provided, so the reading is not
// trivially copyable, and bit_cast refuses it at its constraint.

#include <fixy/os/ClockSource.h>

#include <bit>
#include <cstdint>

int main() {
    auto forged = std::bit_cast<fixy::BootClockBytes<std::uint64_t>>(std::uint64_t{42});
    (void)forged;
    return 0;
}
