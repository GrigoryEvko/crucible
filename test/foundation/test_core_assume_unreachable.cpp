// Tests of assume_unreachable of foundation/core/Fatal.h under UBSan.  The
// point costs nothing in Release, so only a build whose units take UBSan
// can see a call that reaches it: test/foundation/CMakeLists.txt adds this
// test only to such a build.  A child process reaches the point, and UBSan
// ends the child with its report.  The runtime of UBSan ends the child with
// an exit code, not with SIGABRT, so the test reads the report of the child
// door and does not call expect_dies.

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Process.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace {

using ::fixy::TestRunnerCtx;

// A call that the optimizer cannot read, so the call of the point stays.
[[gnu::noipa]] void reach_the_point() noexcept { ::foundation::core::assume_unreachable(); }

// The child that reaches the point ends with no clean exit, and its
// standard error holds the report of UBSan.
void test_the_point_traps_under_ubsan(TestRunnerCtx const& test) {
    ::fixy::process::ChildReport const report =
        ::fixy::process::wait(
            ::fixy::process::spawn_child(test, [] noexcept { reach_the_point(); }).expect("the test starts a child"))
            .expect("the test waits for its child");
    bool const is_reported =
        ::foundation::core::find(report.output(),
                                 ::foundation::core::TextView{"execution reached an unreachable program point"})
            .is_some();
    CRUCIBLE_TEST_ASSERT(test, !report.is_exit_with(0) && is_reported);
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_the_point_traps_under_ubsan(test);
    return 0;
}
