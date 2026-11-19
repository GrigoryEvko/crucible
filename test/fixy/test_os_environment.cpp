// The environment door: the switches that the start-up hook reads.
//
// The CMake list of the test sets each switch of the record before the
// process starts:
//   CRUCIBLE_PERF_QUIET=10            on: a perf switch needs a leading 1
//   CRUCIBLE_PERF_VERBOSE absent      off
//   CRUCIBLE_WARDEN_QUIET=10          off: a warden switch needs the text 1
//   CRUCIBLE_WARDEN_FORCE=1           on
//   CRUCIBLE_PERF_PMU_PERIOD_HW=12345 the period 12345
//   CRUCIBLE_PERF_PMU_PERIOD_IBS=0    no period: zero turns the sampling off
//   CRUCIBLE_PERF_PMU_PERIOD_SW=-5    no period: the grammar refuses a sign
// The values differ between the switches, so a hook that reads a switch
// into the field of a different switch fails a check.  The check also
// proves that the link keeps the hook, and that the hook runs before main.

#include <fixy/Core.h>
#include <fixy/os/Environment.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

[[nodiscard]] int check_switches() noexcept {
    fixy::env::Switches const switches = fixy::env::switches();
    if (!switches.is_perf_quiet) return 1;
    if (switches.is_perf_verbose) return 2;
    if (switches.is_warden_quiet) return 3;
    if (!switches.is_warden_forced) return 4;
    if (fixy::env::switches().pmu_period_hardware.value_or(0) != 12345) return 5;
    if (switches.pmu_period_dynamic.is_some()) return 6;
    if (switches.pmu_period_software.is_some()) return 7;
    return 0;
}

}  // namespace

int main() {
    int const failed = check_switches();
    if (failed != 0) {
        fixy::report(fixy::Sink::Err, "test_os_environment: check {} failed\n", failed);
        return failed;
    }
    ::fixy::check::pass(kTest, "test_os_environment: passed\n");
    return 0;
}
