// Sentinel TU for foundation/Platform.h: the macros expand under the project
// warning set, the assertion and invariant macros fire on a false predicate,
// the debugger stand-ins link, and the contract handler links.

#include <foundation/Platform.h>

#include "abort_probe.h"

// CRUCIBLE_DEBUG_ASSERT and CRUCIBLE_INVARIANT do no check under NDEBUG, and
// a false CRUCIBLE_INVARIANT is then undefined behavior.
#ifdef NDEBUG
#error "test_platform checks the NDEBUG-keyed macros, so it must compile without NDEBUG"
#endif

namespace {

CRUCIBLE_CONST int doubled(int const value) noexcept { return value * 2; }

}  // namespace

int main() {
    using foundation::test::aborts;

    if (doubled(21) != 42) return 1;

    // A true invariant is silent in every mode.
    CRUCIBLE_INVARIANT(doubled(1) == 2);
    CRUCIBLE_FATAL_INVARIANT(doubled(2) == 4);

    // A false fatal invariant aborts in every mode.
    if (!aborts([] { CRUCIBLE_FATAL_INVARIANT(doubled(3) == 0); })) return 2;

    // A P2900 contract violation reaches the weak handler and aborts.
    if (!aborts([] { contract_assert(doubled(4) == 0); })) return 3;

    // A body that does not abort reports so.
    if (aborts([] { CRUCIBLE_FATAL_INVARIANT(doubled(5) == 10); })) return 4;

    // A true assertion is silent.
    CRUCIBLE_ASSERT(doubled(6) == 12);
    CRUCIBLE_DEBUG_ASSERT(doubled(7) == 14);

    // A false assertion or invariant aborts.
    if (!aborts([] { CRUCIBLE_ASSERT(doubled(8) == 0); })) return 5;
    if (!aborts([] { CRUCIBLE_DEBUG_ASSERT(doubled(9) == 0); })) return 6;
    if (!aborts([] { CRUCIBLE_INVARIANT(doubled(10) == 0); })) return 7;

    // The answer depends on how the binary was started, so only the fact
    // that the probe links and returns a defined bool is checked.
    bool const is_traced = ::foundation::detail::is_debugger_present();
    (void)is_traced;

    // The call traps only under a debugger, and the test runner attaches
    // none, so it returns here.
    ::foundation::detail::breakpoint_if_debugging();

    return 0;
}
