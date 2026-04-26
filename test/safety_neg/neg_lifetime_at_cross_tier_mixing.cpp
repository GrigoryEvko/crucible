// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a LifetimeLattice::At<PER_FLEET>::element_type
// to a function expecting LifetimeLattice::At<PER_REQUEST>::element_type.
//
// LifetimeLattice::At<L> is a singleton sub-lattice whose element_type
// is a NESTED struct — its identity depends on the template parameter
// L.  Two At<L1>::element_type and At<L2>::element_type for L1 ≠ L2
// are DISTINCT TYPES (different template instantiations of the inner
// struct), so passing one to a function expecting the other is a
// type-mismatch compile error.
//
// This test pins the type-safety contract: a future refactor that
// (e.g.) shared a single element_type alias across all At<L>
// specializations would silently allow cross-tier mixing — a
// Per-Request value flowing into a Per-Fleet position, defeating
// the type-level lifetime discipline that SessionOpaqueState<T, L>
// rests on.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's overload-resolution
// rejection ("could not convert" / "no matching function for call"),
// not from a framework-owned static_assert string.  Acceptable here
// because the discipline is intrinsic to nested-struct template
// identity, which is a structural C++ property rather than a
// framework invariant.

#include <crucible/algebra/lattices/LifetimeLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    LifetimeLattice::At<Lifetime::PER_REQUEST>::element_type req_elt{};
    LifetimeLattice::At<Lifetime::PER_FLEET>::element_type   fleet_elt{};

    // Should FAIL: At<PER_REQUEST>::leq expects two At<PER_REQUEST>::
    // element_type arguments; fleet_elt is a different type
    // (At<PER_FLEET>::element_type), no implicit conversion.
    return static_cast<int>(
        LifetimeLattice::At<Lifetime::PER_REQUEST>::leq(req_elt, fleet_elt));
}
