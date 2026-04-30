// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D30 fixture (EpochVersioned, second product wrapper) — pins
// constrained-extractor.  epoch_versioned_value_t is constrained on
// `requires is_epoch_versioned_v<T>`.
//
// Single fixture (vs two for NTTP wrappers) — same product-wrapper
// rationale as IsBudgeted: no compile-time tag exists.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsEpochVersioned.h>

int main() {
    using V = crucible::safety::extract::epoch_versioned_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
