// Sentinel TU for foundation/Platform.h: the macros expand under the project
// warning set, the check macro fires on a false predicate, the debugger
// stand-ins link, and the contract handler links.

#include <foundation/Platform.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

CRUCIBLE_CONST int doubled(int const value) noexcept { return value * 2; }

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};

    if (doubled(21) != 42) return 1;

    // A true check is silent in every mode.
    CRUCIBLE_FATAL_INVARIANT(doubled(2) == 4);
    CRUCIBLE_FATAL_INVARIANT(doubled(5) == 10);

    // A false check aborts in every mode.  The check runs in a child through
    // expect_dies of the Check family.
    ::fixy::check::expect_dies(
        test, [] noexcept { CRUCIBLE_FATAL_INVARIANT(doubled(3) == 0); },
        ::foundation::core::TextView{"foundation: fatal invariant failed: doubled(3) == 0"});

    // A P2900 contract violation reaches the weak handler and aborts.
    ::fixy::check::expect_dies(
        test, [] noexcept { contract_assert(doubled(4) == 0); },
        ::foundation::core::TextView{"foundation: contract violation: doubled(4) == 0"});

    // The answer depends on how the binary was started, so only the fact
    // that the probe links and returns a defined bool is checked.
    bool const is_traced = ::foundation::detail::is_debugger_present();
    (void)is_traced;

    // The call traps only under a debugger, and the test runner attaches
    // none, so it returns here.
    ::foundation::detail::breakpoint_if_debugging();

    return 0;
}
