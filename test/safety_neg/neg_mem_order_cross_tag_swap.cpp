// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing MemOrder<TAG_A, T> with MemOrder<TAG_B, T>
// when TAG_A != TAG_B.
//
// swap() takes a reference to the SAME class — a member taking
// `MemOrder<Tag, T>&`.  Cross-tag swap is rejected at overload
// resolution.
//
// Concrete bug-class this catches: a refactor adding cross-tag swap
// would let memory-ordering labels swap independently of value
// bytes — a label vs bytes disjointness allowing seq-cst-fenced
// bytes to flow through a Relaxed-typed slot.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/MemOrder.h>
#include <utility>

using namespace crucible::safety;

int main() {
    MemOrder<MemOrderTag_v::Relaxed, int> relax_value{42};
    MemOrder<MemOrderTag_v::SeqCst,  int> seqcst_value{7};

    // Should FAIL: MemOrder<Relaxed, int>::swap takes
    // MemOrder<Relaxed, int>&; seqcst_value is a different type.
    relax_value.swap(seqcst_value);

    using std::swap;
    swap(relax_value, seqcst_value);

    return relax_value.peek();
}
