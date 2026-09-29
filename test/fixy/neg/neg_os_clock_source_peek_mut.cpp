// A mutable reference to a reading would let a holder of a real reading
// write any value under its source.  ClockSource has no peek_mut, not
// even a private one.
//
// The overwriting function is never called.  The error is at the member
// lookup, so it is reported whether or not a clock was ever read.

#include <fixy/os/ClockSource.h>

#include <cstdint>

namespace {
[[maybe_unused]] void overwrite(fixy::BootClockBytes<std::uint64_t>& reading) { reading.peek_mut() = 7; }
}  // namespace

int main() { return 0; }
