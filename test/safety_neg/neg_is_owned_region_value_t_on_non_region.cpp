// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating owned_region_value_t<T> where T is not an
// OwnedRegion specialization.  The alias is constrained on
// is_owned_region_v<T>, so non-OwnedRegion arguments are rejected at
// the requires clause rather than producing `void` (which would be
// a silent failure).
//
// Concrete bug-class this catches: a refactor that drops the
// `requires is_owned_region_v<T>` constraint on owned_region_value_t
// would let `owned_region_value_t<int>` resolve to `void` from the
// primary template's `value_type = void;`, propagating that void
// silently into downstream type machinery.  With the constraint, the
// alias rejects non-OwnedRegion at the call boundary.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsOwnedRegion.h>

int main() {
    // int is not an OwnedRegion → alias is ill-formed.
    using V = crucible::safety::extract::owned_region_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
