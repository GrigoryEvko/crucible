// Sentinel TU for foundation/Platform.h: the macros expand under the project
// warning set, the check macro fires on a false predicate, the debugger
// stand-ins link, and the contract handler links.

#include <foundation/Platform.h>

#include "abort_probe.h"

namespace {

CRUCIBLE_CONST int doubled(int const value) noexcept { return value * 2; }

}  // namespace

int main() {
    using crucible::test::aborts;

    if (doubled(21) != 42) return 1;

    // A true check is silent in every mode.
    CRUCIBLE_FATAL_INVARIANT(doubled(2) == 4);

    // A false check aborts in every mode.
    if (!aborts([] { CRUCIBLE_FATAL_INVARIANT(doubled(3) == 0); })) return 2;

    // A P2900 contract violation reaches the weak handler and aborts.
    if (!aborts([] { contract_assert(doubled(4) == 0); })) return 3;

    // A body that does not abort reports so.
    if (aborts([] { CRUCIBLE_FATAL_INVARIANT(doubled(5) == 10); })) return 4;

    // The answer depends on how the binary was started, so only the fact
    // that the probe links and returns a defined bool is checked.
    bool const is_traced = ::foundation::detail::is_debugger_present();
    (void)is_traced;

    // The call traps only under a debugger, and the test runner attaches
    // none, so it returns here.
    ::foundation::detail::breakpoint_if_debugging();

    return 0;
}
