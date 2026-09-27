// A clock reader reads a clock through clock_gettime.  The TSC has no
// clock id, so the clock-reader mint refuses the TSC source, and a TSC
// read goes through mint_tsc_reader, which asks for a pin.

#include <fixy/Ctx.h>
#include <fixy/os/Time.h>

int main() {
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    [[maybe_unused]] auto refused = fixy::time::mint_clock_reader<fixy::ClockSource_v::TscRaw>(ctx);
    return 0;
}
