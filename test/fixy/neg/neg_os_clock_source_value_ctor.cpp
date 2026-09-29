// ClockSource<Boot, T> claims that CLOCK_BOOTTIME returned its value.  A
// public value constructor would make any integer a boot-clock reading.
// A boot reading keeps ticking through a suspend, and a deadline gate that
// admits it on that ground would then measure a forged value.
//
// The constructor is private.  Its sole friend is the stamp door in
// fixy/os/Time.h, and only the clock readers reach that door, right after
// they read the clock.  This fixture is the standing witness that the value
// constructor stays private.

#include <fixy/os/ClockSource.h>

#include <cstdint>

int main() {
    fixy::BootClockBytes<std::uint64_t> forged{std::uint64_t{42}};
    (void)forged;
    return 0;
}
