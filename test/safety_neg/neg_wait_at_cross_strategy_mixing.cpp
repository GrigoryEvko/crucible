// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a WaitLattice::At<STRATEGY_B>::element_type to
// a function expecting WaitLattice::At<STRATEGY_A>::element_type.
//
// Per-At<T> nested-struct-identity at the LATTICE substrate.
// Mirrors neg_hot_path_at_cross_tier_mixing /
// neg_det_safe_at_cross_tier_mixing / etc.  A future refactor
// extracting a shared singleton_carrier<WaitStrategy> alias above
// At<T> would silently allow SpinPause-tier and Block-tier values
// to interconvert at the lattice level — bypassing the wrapper's
// strategy-pin via Graded's compose / weaken paths that take
// element_type values directly.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection on the nested-
// struct template identity at the LATTICE surface.

#include <crucible/algebra/lattices/WaitLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    WaitLattice::At<WaitStrategy::SpinPause>::element_type spin_elt{};
    WaitLattice::At<WaitStrategy::Block>::element_type     block_elt{};

    // Should FAIL: At<SpinPause>::leq expects two At<SpinPause>::
    // element_type arguments; block_elt is At<Block>::element_type
    // — different template instantiation, different type.
    return static_cast<int>(
        WaitLattice::At<WaitStrategy::SpinPause>::leq(spin_elt, block_elt));
}
