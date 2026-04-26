// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling ProductLattice<L0, L1, L2>::get<I>(e) with
// I >= sizeof...(Ls).
//
// The N-ary primary's `get<I>` is gated:
//
//   template <std::size_t I>
//       requires (I < sizeof...(Ls))
//   [[nodiscard]] static constexpr auto& get(element_type& e) noexcept;
//
// The requires clause fails at the call site when I exceeds the
// arity, producing a clean concept-failure diagnostic.  Without the
// clause, the static_cast inside would silently reach for a base
// class that doesn't exist on element_type, producing a confusing
// "no matching function for static_cast" cascade.
//
// Pins the per-call bounds discipline.  Symmetric to the
// HappensBefore operator[] bounds-check we shipped earlier:
// pre-conditions on accessors are the front-line defense against
// integer-index slip bugs.
//
// [GCC-text + framework] — concept-failure diagnostic text;
// the requires clause itself is FRAMEWORK-CONTROLLED.

#include <crucible/algebra/lattices/ProductLattice.h>
#include <crucible/algebra/lattices/QttSemiring.h>

using namespace crucible::algebra::lattices;

int main() {
    using L = QttSemiring::At<QttGrade::One>;
    using P = ProductLattice<L, L, L>;   // arity = 3, valid indices: 0, 1, 2
    P::element_type e{};

    // Should FAIL: get<3> exceeds arity (max valid I is 2).
    // requires (I < sizeof...(Ls)) rejects.
    return sizeof(P::get<3>(e));
}
