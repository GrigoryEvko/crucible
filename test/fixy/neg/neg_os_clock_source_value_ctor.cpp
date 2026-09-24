// ClockSource<Boot, T> claims that CLOCK_BOOTTIME returned its value.  The
// value constructor was public, so any integer became a boot-clock reading.
// A boot reading keeps ticking through a suspend, and a deadline gate that
// admits it on that ground then measured a forged value.
//
// The constructor is private now.  Its sole friend is the stamp door in
// fixy/os/Time.h, and only the clock readers reach that door, right after
// they read the clock.  This fixture is the standing witness that the value
// constructor stayed private.

#include <fixy/os/ClockSource.h>

#include <cstdint>

int main() {
    fixy::BootClockBytes<std::uint64_t> forged{std::uint64_t{42}};
    (void)forged;
    return 0;
}
