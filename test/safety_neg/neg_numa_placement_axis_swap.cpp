// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing AffinityMask where NumaNodeId is expected
// (or vice versa) at the NumaPlacement constructor.
//
// THE LOAD-BEARING REJECTION FOR THE PRODUCT-WRAPPER AXIS DISCIPLINE.
// NumaPlacement's constructor is `NumaPlacement(T, NumaNodeId, AffinityMask)`.
// If a maintainer flipped the axes —
//   NumaPlacement(value, affinity, node)   // ← flipped
// — the compile error catches the mistake even though both axes
// are uint-backed (NumaNodeId is a 1-byte enum, AffinityMask is an
// 8-byte struct — but the enum-vs-struct distinction alone is
// insufficient defense at production sites where templated factories
// might bind both types via type-erasure).
//
// [GCC-WRAPPER-TEXT] — constructor parameter-type mismatch.

#include <crucible/safety/NumaPlacement.h>

using namespace crucible::safety;

int main() {
    NumaNodeId   node{2};
    AffinityMask aff{0b11};

    // Should FAIL: NumaPlacement<int>(int, NumaNodeId, AffinityMask)
    // cannot accept (int, AffinityMask, NumaNodeId) — axes flipped.
    NumaPlacement<int> bad{42, aff, node};
    return bad.peek();
}
