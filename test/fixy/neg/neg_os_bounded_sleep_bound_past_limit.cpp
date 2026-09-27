// A sleep deadline is the time of the monotonic clock plus the sleep, in
// nanoseconds, and the sum must fit in 64 bits.  So a sleeper refuses a
// bound above max_bounded_sleep_nanos, and the sleep mint has no
// candidate for such a bound.

#include <fixy/Ctx.h>
#include <fixy/os/Time.h>

int main() {
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    [[maybe_unused]] auto refused = fixy::time::mint_bounded_sleep<fixy::time::max_bounded_sleep_nanos + 1>(ctx);
    return 0;
}
