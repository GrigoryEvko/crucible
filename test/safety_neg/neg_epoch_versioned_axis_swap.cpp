// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing Generation where Epoch is expected (or vice
// versa) at the EpochVersioned constructor.
//
// THE LOAD-BEARING REJECTION FOR THE PRODUCT-WRAPPER AXIS DISCIPLINE.
// EpochVersioned's constructor is `EpochVersioned(T, Epoch, Generation)`.
// If a maintainer flipped the axes —
//   EpochVersioned(value, generation, epoch)   // ← flipped
// — and BOTH Epoch and Generation were just `uint64_t`, the bug
// would compile silently and downstream Canopy reshard validation
// gates would compare generations against the fleet epoch — semantically
// catastrophic (a per-Relay-local counter compared against the
// cluster-wide Raft commit log).
//
// The strong-typed Epoch and Generation newtypes (each a distinct
// C++ struct phantom-tagged with its purpose) make this flip a
// compile error: Generation is NOT implicitly convertible to Epoch,
// even though both wrap uint64_t.
//
// [GCC-WRAPPER-TEXT] — constructor parameter-type mismatch.

#include <crucible/safety/EpochVersioned.h>

using namespace crucible::safety;

int main() {
    Epoch      ep{5};
    Generation gen{2};

    // Should FAIL: EpochVersioned<int>(int, Epoch, Generation) cannot
    // accept (int, Generation, Epoch) — axes are flipped.
    EpochVersioned<int> bad{42, gen, ep};
    return bad.peek();
}
