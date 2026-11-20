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
//   TMPDIR=/var/tmp/                  the checked path /var/tmp/
//   DEBUGINFOD_URLS absent            off
// The launcher of the tests drops DEBUGINFOD_URLS, so no test can set it.
// The values differ between the switches, so a hook that reads a switch
// into the field of a different switch fails a check.  The check also
// proves that the link keeps the hook, and that the hook runs before main.
// The cases of check_temporary_directory give the check of TMPDIR each text
// that it refuses, and the longest path that it takes.

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
    bool const is_tmpdir_read = switches.temporary_directory.match(
        [](fixy::env::TemporaryDirectoryPath const& path) noexcept {
            return path.c_text().view() == fixy::TextView{"/var/tmp/"};
        },
        [] noexcept { return false; });
    if (!is_tmpdir_read) return 8;
    if (switches.has_debuginfod_server) return 9;
    return 0;
}

// True when the check takes text as its path, with no change.
[[nodiscard]] bool is_taken(fixy::TextView text) noexcept {
    return fixy::env::check_temporary_directory(text).match(
        [text](fixy::env::TemporaryDirectoryPath path) noexcept { return path.c_text().view() == text; },
        [] noexcept { return false; });
}

// True when the check refuses text.
[[nodiscard]] bool is_refused(fixy::TextView text) noexcept {
    return fixy::env::check_temporary_directory(text).is_none();
}

[[nodiscard]] int check_temporary_directory_cases() noexcept {
    if (!is_taken("/tmp")) return 21;
    if (!is_taken("/")) return 22;
    if (!is_taken("/a/..b/./c")) return 23;
    if (!is_refused("relative/dir")) return 24;
    if (!is_refused("")) return 25;
    if (!is_refused("/a/../b")) return 26;
    if (!is_refused("/a/..")) return 27;
    // The longest path that the check takes, and one character more.
    fixy::FixedText<fixy::env::kTemporaryDirectoryBytes + 1> longest{};
    longest.append('/').expect("the text holds the slash");
    while (longest.size() < fixy::env::kTemporaryDirectoryBytes) {
        longest.append('a').expect("the text holds each character");
    }
    if (!is_taken(longest.view())) return 28;
    longest.append('a').expect("the text holds one character more");
    if (!is_refused(longest.view())) return 29;
    return 0;
}

}  // namespace

int main() {
    int failed = check_switches();
    if (failed == 0) failed = check_temporary_directory_cases();
    if (failed != 0) {
        fixy::report(fixy::Sink::Err, "test_os_environment: check {} failed\n", failed);
        return failed;
    }
    ::fixy::check::pass(kTest, "test_os_environment: passed\n");
    return 0;
}
