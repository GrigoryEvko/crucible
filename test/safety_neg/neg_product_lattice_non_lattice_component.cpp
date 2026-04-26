// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating ProductLattice<...> with a component type
// that does NOT satisfy the Lattice concept.
//
// The N-ary primary template carries:
//
//   static_assert((Lattice<Ls> && ...),
//       "ProductLattice<Ls...>: every L_i must satisfy the Lattice "
//       "concept.");
//
// — this fires at template-instantiation time when ANY Ls is not a
// Lattice.  Without it, downstream callers would see a cascade of
// "no member 'leq'" / "no member 'join'" diagnostics deep in the
// fold expressions, hiding the actual violation (a non-Lattice
// component) behind ten layers of template noise.
//
// Pins the up-front concept gate: a future refactor that removed the
// static_assert (or accidentally narrowed the concept check via
// SFINAE rather than concept conjunction) would silently allow
// non-Lattice types to flow into the variadic primary, producing
// confusing downstream errors instead of a clear named diagnostic.
//
// [FRAMEWORK-CONTROLLED] — diagnostic regex matches the static_assert
// string in the primary template.

#include <crucible/algebra/lattices/ProductLattice.h>
#include <crucible/algebra/lattices/QttSemiring.h>

using namespace crucible::algebra::lattices;

namespace {
// A type that is NOT a Lattice — has none of leq/join/meet members.
struct NotALattice {
    using element_type = int;
    // Deliberately missing leq, join, meet, bottom, top.
};
}  // namespace

int main() {
    // Should FAIL: NotALattice doesn't satisfy Lattice concept.
    // The primary template's static_assert fires on instantiation.
    // 3-way arity (not 2) so the variadic primary is selected over
    // the binary specialization (which has its own concept gate).
    using P = ProductLattice<QttSemiring::At<QttGrade::One>,
                             NotALattice,
                             QttSemiring::At<QttGrade::One>>;
    return sizeof(P::element_type);
}
