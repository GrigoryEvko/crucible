// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a HotPathLattice::At<TIER_B>::element_type to
// a function expecting HotPathLattice::At<TIER_A>::element_type.
//
// Symmetric to the per-At<T> cross-tier-mixing fixtures shipped for
// Tolerance / Consistency / Lifetime / DetSafe.  Pins the per-At<T>
// sub-lattice element_type identity at the LATTICE substrate.  Each
// At<T> sub-lattice's element_type is a NESTED struct whose template
// identity depends on T; cross-tier mixing must be a type-mismatch.
//
// THIS IS THE LATTICE-LEVEL MIRROR of neg_hot_path_relax_to_stronger.
// The latter pins per-tier identity at the WRAPPER surface (relax<>);
// THIS test pins it at the LATTICE substrate.  Both surfaces must
// reject cross-tier mixing or the hot-path admission gate could be
// silently defeated:
//
//   - Refactor extracting a shared `singleton_carrier<HotPathTier>`
//     template alias above HotPathLattice::At<T> would make
//     At<Hot>::element_type and At<Cold>::element_type the same C++
//     type.  Hot-tier and Cold-tier values would silently
//     interconvert at the lattice level.
//   - The wrapper-surface relax<>() rejection would still fire for
//     direct Cold→relax<Hot> calls, but a lattice-level mix could
//     leak through Graded's compose / weaken paths that take
//     element_type values directly.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection on the nested-
// struct template identity at the LATTICE surface.

#include <crucible/algebra/lattices/HotPathLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    HotPathLattice::At<HotPathTier::Hot>::element_type  hot_elt{};
    HotPathLattice::At<HotPathTier::Cold>::element_type cold_elt{};

    // Should FAIL: At<Hot>::leq expects two At<Hot>::element_type
    // arguments; cold_elt is At<Cold>::element_type — different
    // template instantiation, different type, no implicit conversion.
    return static_cast<int>(
        HotPathLattice::At<HotPathTier::Hot>::leq(hot_elt, cold_elt));
}
