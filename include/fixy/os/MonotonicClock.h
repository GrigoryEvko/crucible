#pragma once

// The clock door of the base: the reads of the clocks of the kernel, and the
// sleep to a deadline of CLOCK_MONOTONIC.
//
// The functions are in detail, because a clock read on the replay-bound
// foreground path makes replay diverge across machines.  fixy/os/Time.h gives
// the readers and the sleeper that code outside the base mints with a
// context, and they call read_clock_ and sleep_monotonic_.  Each read of a
// clock goes through a reader, so each read takes the tag of Nondet.  The one
// other caller is the start-up hook of src/fixy/os/SpinBudget.cpp, which has
// no context.  It calibrates the counter of the core, and its readings leave
// the hook only as the rate of the spin budget.  Each deadline of that budget
// takes the tag of Nondet.
//
// This header is in the layer X3_os of utils/rule-table.txt.  The door file
// is src/fixy/os/MonotonicClock.cpp, the one includer of <time.h>.

#include <foundation/core/Choice.h>
#include <foundation/os/Errno.h>
#include <foundation/os/FutexWords.h>

#include <cstdint>

namespace fixy::time {

// The id of a clock of the kernel, the clockid_t of the C library.  The named
// values are the clocks that the readers of fixy/os/Time.h read, and the door
// file compares each value with <time.h>.  A PTP hardware clock has an id of
// its own, which its descriptor gives (fixy/os/Time.h).  unbacked names no
// clock: no clock of the kernel serves a source with it.
enum class ClockId : std::int32_t {
    unbacked = -1,
    realtime = 0,
    monotonic = 1,
    process_cpu = 2,
    thread_cpu = 3,
    monotonic_raw = 4,
    boot = 7,
};

}  // namespace fixy::time

namespace fixy::time::detail {

// The reading of the clock CLOCK, as the kernel gives it, or the error of a
// failed read.  The door does not check the fields of the reading, and
// fixy/os/Time.h converts them.
[[nodiscard]] ::foundation::core::Result<::foundation::os::KernelTimespec, ::foundation::os::Errno>
read_clock_(ClockId clock) noexcept;

// Sleeps to the time of CLOCK_MONOTONIC NANOS after now, and gives the error
// of a failed read or sleep.  A signal does not end the sleep before the
// deadline, and the time already slept does not move the deadline.  A
// reading that gives no deadline in 64 bits gives eoverflow, and a reading
// with a nanosecond field out of range gives einval, the answer of the kernel
// for such a deadline.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sleep_monotonic_(std::uint64_t nanos) noexcept;

}  // namespace fixy::time::detail
