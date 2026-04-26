// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a ConsistencyLattice::At<TIER_B>::element_type
// to a function expecting ConsistencyLattice::At<TIER_A>::element_type.
//
// Symmetric to neg_lifetime_at_cross_tier_mixing.cpp but pinned across
// the second of the three new chain-lattice families (Lifetime,
// Consistency, Tolerance).  Each At<L> sub-lattice's element_type is
// a NESTED struct whose template identity depends on L; cross-tier
// mixing must be a type-mismatch.
//
// Without this test, a future refactor that quietly merged the per-L
// element_type definitions (e.g. by extracting a shared
// `singleton_carrier` template alias above the lattices) would silently
// allow Per-EVENTUAL-tier code to flow into Per-STRONG-tier positions
// — defeating the BatchPolicy<Axis, Level> per-axis discipline that
// pins the consistency level at the type level.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection on the nested-
// struct template identity.

#include <crucible/algebra/lattices/ConsistencyLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    ConsistencyLattice::At<Consistency::EVENTUAL>::element_type eventual_elt{};
    ConsistencyLattice::At<Consistency::STRONG>::element_type   strong_elt{};

    // Should FAIL: At<EVENTUAL>::leq expects two At<EVENTUAL>::
    // element_type arguments; strong_elt is At<STRONG>::element_type
    // — different template instantiation, different type, no implicit
    // conversion.
    return static_cast<int>(
        ConsistencyLattice::At<Consistency::EVENTUAL>::leq(eventual_elt, strong_elt));
}
