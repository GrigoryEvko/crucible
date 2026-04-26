// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a ProductLattice<L1, L2, L3>::element_type to a
// function expecting ProductLattice<L1, L2>::element_type (or vice
// versa) — cross-arity mixing.
//
// Each ProductLattice<Ls...>::element_type is a unique
// ProductElementImpl<index_sequence<...>, Ls...> instantiation —
// different (Ls...) packs give different template specializations,
// hence different element_type identities.  A 3-way product's
// element is structurally a different type from a 2-way product's
// element, even when the first two component lattices are the same.
//
// Pins the per-arity type-identity contract.  Catches the same class
// of bug the chain-lattice cross-At tests catch (LifetimeLattice::
// At<L1> vs At<L2>): a refactor that added an implicit converting
// constructor between ProductElement specializations would silently
// allow a 3-way grade to flow into a 2-way position, dropping or
// padding components.
//
// Note: this also crosses the variadic-primary boundary with the
// binary specialization.  ProductLattice<L1, L2> uses the binary
// specialization (different storage layout — first/second members);
// ProductLattice<L1, L2, L3> uses the N-ary primary (inheritance-
// based slots).  The two element_types are STRUCTURALLY DIFFERENT
// at every level, so the cross-arity rejection is doubly enforced.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection on the
// per-instantiation element_type identity.

#include <crucible/algebra/lattices/ProductLattice.h>
#include <crucible/algebra/lattices/QttSemiring.h>

using namespace crucible::algebra::lattices;

int main() {
    using L = QttSemiring::At<QttGrade::One>;
    using P2 = ProductLattice<L, L>;          // binary specialization
    using P3 = ProductLattice<L, L, L>;       // N-ary primary

    P2::element_type pair_elt{};
    P3::element_type triple_elt{};

    // Should FAIL: P2::leq expects two P2::element_type arguments;
    // triple_elt is P3::element_type — different specialization,
    // different storage layout, no implicit conversion.
    return static_cast<int>(P2::leq(pair_elt, triple_elt));
}
