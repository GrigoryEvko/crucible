// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-ALGEBRA fixture #4: BoundedLattice concept rejects a lattice
// that has bottom() but lacks top().
//
// Violation: routing through `fixy::algebra::BoundedLattice<L>` must
// reject `HalfBoundedLattice` because `BoundedLattice = BoundedBelow &&
// BoundedAbove`; a lattice with only bottom() satisfies
// BoundedBelowLattice but FAILS BoundedAboveLattice (and therefore
// BoundedLattice).
//
// Distinct from fixtures #1-#3:
//   #1 non_lattice          — rejects on missing element_type / leq / join / meet
//   #2 non_graded           — rejects on bare type not being Graded<...>
//   #3 semiring_missing_zero — rejects on missing additive identity
//   #4 bounded_missing_top   — rejects on partial-bounded gate (this fixture)
//
// Rationale: the Graded<M, L, T> wrappers in canonical wrapper-nesting
// order (HotPath / DetSafe / CipherTier / ResidencyHeat / Vendor /
// AllocClass / Wait / MemOrder / Progress) require BoundedLattice
// because their grade EBO-collapses against the lattice's top/bottom
// pinning.  A lattice that is only half-bounded silently breaks the
// EBO-collapse promise; the BoundedLattice gate exists precisely to
// catch this at substitution time.
//
// Expected diagnostic: GCC's "associated constraints are not satisfied"
// pointing at the BoundedAboveLattice / BoundedLattice concept's
// `L::top()` clause.

#include <crucible/fixy/Algebra.h>

#include <string_view>

namespace fa = crucible::fixy::algebra;

// Carrier type unique to this fixture.  Has bottom() but NO top().
struct AlgebraNegFixture4_HalfBounded {
    using element_type = int;
    static constexpr bool leq(int a, int b) noexcept { return a <= b; }
    static constexpr int  join(int a, int b) noexcept { return a > b ? a : b; }
    static constexpr int  meet(int a, int b) noexcept { return a < b ? a : b; }
    static constexpr int  bottom() noexcept { return 0; }
    // INTENTIONALLY MISSING: static constexpr int top() noexcept;
    static constexpr std::string_view name() noexcept {
        return "AlgebraNegFixture4_HalfBounded";
    }
};

int main() {
    // BoundedLattice gate must reject — missing top() identity.
    static_assert(fa::BoundedLattice<AlgebraNegFixture4_HalfBounded>,
        "fa::BoundedLattice<HalfBounded> must reject — no top() "
        "identity.  fixy::algebra alias preserves the substrate's "
        "concept gate; a half-bounded lattice silently breaks the "
        "EBO-collapse promise of HotPath / DetSafe / CipherTier "
        "Graded wrappers.");
    return 0;
}
