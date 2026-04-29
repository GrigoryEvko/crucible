// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing Generation where Epoch is expected at the
// EpochVersioned<T>::is_at_least(Epoch, Generation) admission gate.
//
// COMPANION TO neg_epoch_versioned_axis_swap (which fences the
// constructor signature).  is_at_least() is the production
// admission-gate signature.  At Canopy reshard / Keeper recovery
// sites:
//
//   if (!checkpoint.is_at_least(committed_epoch, my_generation))
//       return reject_stale_checkpoint();
//
// A flipped-axis call —
//
//   if (!checkpoint.is_at_least(my_generation, committed_epoch))
//
// — would silently mask staleness if Epoch ≡ Generation ≡ uint64_t.
// The strong-typed newtypes make the flip a compile error.
//
// [GCC-WRAPPER-TEXT] — is_at_least parameter-type rejection.

#include <crucible/safety/EpochVersioned.h>

using namespace crucible::safety;

int main() {
    EpochVersioned<int> checkpoint{42, Epoch{5}, Generation{2}};

    Epoch      committed_epoch{4};
    Generation my_generation{1};

    // Should FAIL: is_at_least(Epoch, Generation) requires axes
    // in declared order; passing (Generation, Epoch) is a type
    // mismatch on both arguments.
    return static_cast<int>(
        checkpoint.is_at_least(my_generation, committed_epoch));
}
