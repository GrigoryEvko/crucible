// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing AffinityMask where NumaNodeId is expected at
// the NumaPlacement<T>::admits(NumaNodeId, AffinityMask) admission
// gate.
//
// COMPANION TO neg_numa_placement_axis_swap (which fences the
// constructor signature).  admits() is the production
// admission-gate signature at AdaptiveScheduler dispatch sites.
// A flipped-axis call —
//
//   if (!task.admits(my_affinity, target_node))   // ← flipped
//
// — is a compile error.
//
// [GCC-WRAPPER-TEXT] — admits parameter-type rejection.

#include <crucible/safety/NumaPlacement.h>

using namespace crucible::safety;

int main() {
    NumaPlacement<int> task{42, NumaNodeId{2}, AffinityMask{0b1100}};

    NumaNodeId   target_node{2};
    AffinityMask my_affinity{0b0100};

    // Should FAIL: admits(NumaNodeId, AffinityMask) requires axes
    // in declared order; passing (AffinityMask, NumaNodeId) is a
    // type mismatch on both arguments.
    return static_cast<int>(
        task.admits(my_affinity, target_node));
}
