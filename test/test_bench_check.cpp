// A setup check of a bench fires in each build.
//
// The presets that build the benches (bench, pgo-generate and pgo) define
// NDEBUG, so assert checks nothing in a bench.  The target of this file
// defines NDEBUG on the command line, as those builds do, and a failed
// CRUCIBLE_BENCH_CHECK must still stop the process with SIGABRT.  Its
// message gives the file, the line and the condition.  The failed check
// runs in a child process through expect_dies of the Check family, and the
// check that holds runs in the test process.

#include "bench_harness.h"

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdlib>

namespace {

// The value comes from the environment, so the compiler cannot fold the
// check away.
void fail_a_setup_check() {
    const int setup_value = (std::getenv("CRUCIBLE_BENCH_CHECK_NEVER_SET") == nullptr) ? 2 : 3;
    CRUCIBLE_BENCH_CHECK(setup_value == 3);
}

void pass_a_setup_check() {
    const int setup_value = (std::getenv("CRUCIBLE_BENCH_CHECK_NEVER_SET") == nullptr) ? 2 : 3;
    CRUCIBLE_BENCH_CHECK(setup_value == 2);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(test, fail_a_setup_check, ::fixy::TextView{"test_bench_check.cpp:"});
    ::fixy::check::expect_dies(test, fail_a_setup_check, ::fixy::TextView{"setup_value == 3"});
    pass_a_setup_check();
    ::fixy::check::pass(test,
                        "test_bench_check: a failed check aborts under NDEBUG, and a check that holds does not\n");
    return 0;
}
