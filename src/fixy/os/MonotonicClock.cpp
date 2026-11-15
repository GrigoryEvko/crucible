// The clock door of the base (fixy/os/MonotonicClock.h).  It is the one
// includer of <time.h>.
//
// A read goes through clock_gettime of the C library, which reads the clocks
// of the vDSO in user space with no system call (decision D12).  The C library
// gives the error of a failed read only in errno.  The base reads errno only
// in src/foundation/os/Intercepted.cpp: <errno.h> has that one door (rule R4),
// and the rule artifact-symbols gives __errno_location that one door.  The
// vDSO gives each read that it cannot do to the system call, so the error of
// a failed read comes from the kernel.  The door then asks the kernel once
// more through the raw system call primitive, and it gives the word of the
// kernel.
//
// A sleep goes through clock_nanosleep raw (the owner decision of
// 2026-11-14), because the call has no path of the vDSO.  In a build with
// ThreadSanitizer, the primitive marks the sleep as a blocking call
// (THE SANITIZERS of foundation/os/Syscall.h).

#include <fixy/os/MonotonicClock.h>

#include <foundation/Platform.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/os/Syscall.h>

#include <time.h>

#include <cstdint>
#include <type_traits>

namespace fixy::time::detail {

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

using Reading = core::Result<os::KernelTimespec, os::Errno>;
using Slept = core::Result<core::Unit, os::Errno>;

static_assert(std::is_same_v<::clockid_t, std::int32_t>, "the clock id of read_clock_ is the clockid_t of <time.h>");

constexpr std::int64_t kNanosecondsPerSecond = 1000000000;
constexpr long kInterrupted = -static_cast<long>(os::Errno::eintr);

// The second read of a clock after a failed read of the C library.  It gives
// the error of the kernel, or the reading if the kernel gives one.
[[nodiscard]] CRUCIBLE_COLD Reading ask_the_kernel(std::int32_t clock) noexcept {
    os::KernelTimespec reading{};
    long const word = os::raw_syscall(os::Syscall::clock_gettime, clock, os::Word{static_cast<void const*>(&reading)});
    return os::result_of(word).match([&reading](std::uint64_t) noexcept -> Reading { return reading; },
                                     [](os::Errno error) noexcept -> Reading { return core::err(error); });
}

// The time of CLOCK_MONOTONIC NANOS after the reading NOW.  The kernel
// refuses a nanosecond field outside of one second with EINVAL, and a sum
// past 64 bits has no deadline.
[[nodiscard]] Reading deadline_after(os::KernelTimespec const& now, std::uint64_t nanos) noexcept {
    if (now.nanoseconds < 0 || now.nanoseconds >= kNanosecondsPerSecond) {
        return core::err(os::Errno::einval);
    }
    auto const per_second = static_cast<std::uint64_t>(kNanosecondsPerSecond);
    auto const now_nanos = static_cast<std::uint64_t>(now.nanoseconds);
    CRUCIBLE_TRY(total, core::checked_add(now_nanos, nanos).ok_or(os::Errno::eoverflow));
    auto const carried_seconds = static_cast<std::int64_t>(total / per_second);
    CRUCIBLE_TRY(seconds, core::checked_add(now.seconds, carried_seconds).ok_or(os::Errno::eoverflow));
    return os::KernelTimespec{.seconds = seconds, .nanoseconds = static_cast<std::int64_t>(total % per_second)};
}

// Sleeps to DEADLINE on CLOCK_MONOTONIC.  A signal handler that runs during
// the sleep ends the call with EINTR, and the sleep then continues to the
// same deadline.  So the loop ends only at the deadline or at another error.
[[nodiscard]] Slept sleep_until(os::KernelTimespec const& deadline) noexcept {
    for (;;) {
        long const word = os::raw_syscall(os::Syscall::clock_nanosleep, CLOCK_MONOTONIC, TIMER_ABSTIME,
                                          os::Word{static_cast<void const*>(&deadline)}, os::Word{});
        if (word != kInterrupted) {
            return os::unit_of(word);
        }
    }
}

}  // namespace

std::uint64_t monotonic_nanos_() noexcept {
    ::timespec now{};
    if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000000U + static_cast<std::uint64_t>(now.tv_nsec);
}

Reading read_clock_(std::int32_t clock) noexcept {
    ::timespec reading{};
    if (::clock_gettime(clock, &reading) != 0) [[unlikely]] {
        return ask_the_kernel(clock);
    }
    return os::KernelTimespec{.seconds = reading.tv_sec, .nanoseconds = reading.tv_nsec};
}

Slept sleep_monotonic_(std::uint64_t nanos) noexcept {
    CRUCIBLE_TRY(now, read_clock_(CLOCK_MONOTONIC));
    CRUCIBLE_TRY(deadline, deadline_after(now, nanos));
    return sleep_until(deadline);
}

}  // namespace fixy::time::detail
