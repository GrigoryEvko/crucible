// TscMode has no enumerator SteadyClockFallback.  Its comment claimed the
// nanoseconds of std::chrono::steady_clock, but a TSC reader of that mode
// read the counter and stamped the source TscRaw.  So the enumerator was a
// copy of Raw under a false contract, and a name of it does not compile.

#include <fixy/os/Time.h>

int main() {
    [[maybe_unused]] constexpr auto mode = fixy::time::TscMode::SteadyClockFallback;
    return 0;
}
