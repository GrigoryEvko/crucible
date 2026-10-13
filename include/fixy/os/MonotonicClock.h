#pragma once

// The monotonic clock door of the base: the nanoseconds of CLOCK_MONOTONIC,
// for a timeout of a runtime check of the base.
//
// The read is in detail, because a clock read on the replay-bound foreground
// path makes replay diverge across machines.  fixy/os/Time.h gives the
// readers that code outside the base mints with a context.  The one caller of
// this door is the watch of fixy/session/Watch.h, which times a wait to find
// a deadlock and gives no result that a replay reads.
//
// This header is in the layer X3_os of utils/rule-table.txt.  The door file
// is src/fixy/os/MonotonicClock.cpp, the one includer of <time.h>.

#include <cstdint>

namespace fixy::time::detail {

// The nanoseconds of CLOCK_MONOTONIC, the clock that std::chrono::steady_clock
// reads on Linux.  A read of this clock does not fail on Linux.  If one
// fails, the result is zero.
[[nodiscard]] std::uint64_t monotonic_nanos_() noexcept;

}  // namespace fixy::time::detail
