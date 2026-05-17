// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-ALGEBRA fixture #3: Semiring concept rejects a structure that
// publishes element_type + add/mul + one() but omits zero().
//
// Violation: routing through `fixy::algebra::Semiring<L>` must reject
// `MissingZeroLattice` because the substrate's Semiring concept
// requires `{ S::zero() } -> std::same_as<LatticeElement<S>>`.
//
// Distinct from fixture #1 (`non_lattice`): #1 rejects on the underlying
// Lattice concept (no element_type / leq / join / meet); #3 rejects on
// the Semiring's algebraic-identity requirement.  A lattice that meets
// the Lattice concept but lacks zero() admits join/meet computations
// but cannot serve as a semiring carrier — the FIXY-AUDIT gate must
// catch the missing additive identity at type-check time.
//
// Expected diagnostic: GCC's "associated constraints are not satisfied"
// pointing at the Semiring concept's `S::zero()` clause.

#include <crucible/fixy/Algebra.h>

#include <string_view>

namespace fa = crucible::fixy::algebra;

// Carrier type unique to this fixture.  Meets Lattice + Semiring's
// add/mul/one() obligations but DOES NOT provide zero().
struct AlgebraNegFixture3_MissingZero {
    using element_type = int;
    static constexpr bool leq(int a, int b) noexcept { return a <= b; }
    static constexpr int  join(int a, int b) noexcept { return a > b ? a : b; }
    static constexpr int  meet(int a, int b) noexcept { return a < b ? a : b; }
    static constexpr int  add(int a, int b) noexcept { return a + b; }
    static constexpr int  mul(int a, int b) noexcept { return a * b; }
    // INTENTIONALLY MISSING: static constexpr int zero() noexcept;
    static constexpr int  one()  noexcept { return 1; }
    static constexpr std::string_view name() noexcept {
        return "AlgebraNegFixture3_MissingZero";
    }
};

int main() {
    // The Semiring concept gate must reject — no zero() identity.
    static_assert(fa::Semiring<AlgebraNegFixture3_MissingZero>,
        "fa::Semiring<MissingZero> must reject — no zero() identity.  "
        "fixy::algebra alias preserves the substrate's concept gate; a "
        "Semiring without an additive identity is structurally broken "
        "and cannot serve as a Graded<> lattice parameter.");
    return 0;
}
