// The monotonic clock door of the base (fixy/os/MonotonicClock.h).  It is the
// one includer of <time.h>, and it reads the clock through clock_gettime of
// the C library, which reads the clock in user space with no system call.

#include <fixy/os/MonotonicClock.h>

#include <time.h>

#include <cstdint>

namespace fixy::time::detail {

std::uint64_t monotonic_nanos_() noexcept {
    ::timespec now{};
    if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000000U + static_cast<std::uint64_t>(now.tv_nsec);
}

}  // namespace fixy::time::detail
