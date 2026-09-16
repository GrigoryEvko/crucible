// Sentinel TU for foundation/Platform.h: the macros expand under the project
// warning set, the invariant family calls the foundation failure arm, and the
// contract handler links.

#include <foundation/Platform.h>

#include "abort_probe.h"

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

    return 0;
}
