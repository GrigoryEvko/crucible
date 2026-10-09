#pragma once

// The argument words of a futex call of the futex door of fixy/os/Futex.h:
// the operations, the bitset of a wait, and the time of a deadline.
//
// The values are the values of <linux/futex.h>, and the layout of
// KernelTimespec is the layout of struct __kernel_timespec of
// <linux/time_types.h>, on x86_64 and aarch64.  The door file
// src/foundation/os/Syscall.cpp compares each one with those headers, so the
// futex door includes no header of the kernel.
//
// This header is in the floor layer F0_os of utils/rule-table.txt.

#include <cstdint>

namespace foundation::os {

// A wait that the deadline in the fourth word ends, with the bitset in the
// sixth word, on a word of one process (FUTEX_WAIT_BITSET_PRIVATE).  The
// deadline is an absolute time of CLOCK_MONOTONIC, and a wait with no
// deadline has the address zero there.
inline constexpr std::uint32_t kFutexWaitBitsetPrivate = 9U | 128U;

// A wake of at most the count in the third word of the waiters, on a word of
// one process (FUTEX_WAKE_PRIVATE).
inline constexpr std::uint32_t kFutexWakePrivate = 1U | 128U;

// The bitset of a wait that each wake matches (FUTEX_BITSET_MATCH_ANY).
inline constexpr std::uint32_t kFutexBitsetMatchAny = 0xFFFFFFFFU;

// An absolute time of a clock, in the layout of struct __kernel_timespec.
struct KernelTimespec {
    std::int64_t seconds = 0;
    std::int64_t nanoseconds = 0;
};

}  // namespace foundation::os
