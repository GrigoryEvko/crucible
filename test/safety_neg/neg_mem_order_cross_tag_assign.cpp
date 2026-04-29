// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning a MemOrder<TAG_A, T> to a
// MemOrder<TAG_B, T> when TAG_A != TAG_B.
//
// Different Tag template arguments produce DIFFERENT class
// instantiations.  No converting assignment, no implicit conversion
// — the type system enforces per-tag disjointness at the
// assignment surface.
//
// Concrete bug-class this catches: a refactor that added a
// templated converting-assign operator on MemOrder would let a
// SeqCst-tier value silently flow into a Relaxed-tier slot.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/MemOrder.h>

using namespace crucible::safety;

int main() {
    MemOrder<MemOrderTag_v::Relaxed, int> relax_value{42};
    MemOrder<MemOrderTag_v::SeqCst,  int> seqcst_value{7};

    // Should FAIL: relax_value and seqcst_value are DIFFERENT types.
    relax_value = seqcst_value;
    return relax_value.peek();
}
