// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a MemOrderLattice::At<TAG_B>::element_type to
// a function expecting MemOrderLattice::At<TAG_A>::element_type.
//
// Per-At<T> nested-struct-identity at the LATTICE substrate.
// Mirrors neg_wait_at_cross_strategy_mixing /
// neg_hot_path_at_cross_tier_mixing / etc.  A future refactor
// extracting a shared singleton_carrier<MemOrderTag> alias above
// At<T> would silently allow Relaxed-tier and SeqCst-tier values
// to interconvert at the lattice level — bypassing the wrapper's
// tag-pin via Graded's compose / weaken paths.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection on the nested-
// struct template identity at the LATTICE surface.

#include <crucible/algebra/lattices/MemOrderLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    MemOrderLattice::At<MemOrderTag::Relaxed>::element_type relax_elt{};
    MemOrderLattice::At<MemOrderTag::SeqCst>::element_type  seqcst_elt{};

    // Should FAIL: At<Relaxed>::leq expects two At<Relaxed>::
    // element_type arguments; seqcst_elt is At<SeqCst>::element_type.
    return static_cast<int>(
        MemOrderLattice::At<MemOrderTag::Relaxed>::leq(relax_elt, seqcst_elt));
}
