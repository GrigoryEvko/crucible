// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Phase 0 P0-3 fixture #1 of 2 for safety::DimensionTraits.h
// (#1094) — proves that the Tier-S concept gate `SemiringGrade<G>`
// rejects a type that is a lattice but NOT a semiring.
//
// The substrate has a deliberate trap: every Semiring is also a
// Lattice (since the lattice carrier underlies the semiring), but
// the converse is FALSE.  A reviewer who weakens `SemiringGrade`
// down to `requires algebra::Lattice<G>` would silently admit
// pure lattices into the par/seq sites where Tier-S composition
// (par=+, seq=*, 0 annihilator) is required, producing wrong
// composition results at runtime.  The neg-compile fixture is the
// regression guard: a lattice-only type MUST be rejected at
// template-substitution time.
//
// The witness type — a boolean lattice with join/meet but NO
// add/mul — already lives inside detail::dimension_traits_self_test
// as `TestLattice`.  This fixture re-defines it standalone (the
// detail type isn't reachable across TUs without crossing the
// detail boundary, and reachability of detail types is a separate
// review topic) and probes the Tier-S concept directly.
//
// Expected diagnostic: "constraints not satisfied" pointing at
// the SemiringGrade<G> concept evaluation.

#include <crucible/safety/DimensionTraits.h>

namespace neg = crucible::safety;

// Boolean lattice with join/meet/leq/bottom/top but NO add/mul/zero/one.
// Algebraically a lattice; algebraically NOT a semiring.
struct LatticeOnly {
    using element_type = bool;
    [[nodiscard]] static constexpr bool bottom() noexcept { return false; }
    [[nodiscard]] static constexpr bool top()    noexcept { return true;  }
    [[nodiscard]] static constexpr bool leq (bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
    // Deliberately missing: zero(), one(), add(), mul().
};

// Probe — a function template that consumes the SemiringGrade gate.
// Instantiating with LatticeOnly forces the compiler to evaluate the
// requires-clause against a type that satisfies LatticeGrade but NOT
// SemiringGrade — substitution failure with structured diagnostic.
template <neg::SemiringGrade G>
constexpr bool consumes_semiring() noexcept {
    return G::add(G::zero(), G::one()) == G::one();
}

// Bridge fires: instantiation site demands SemiringGrade<LatticeOnly>;
// the substrate's add/mul/zero/one absence makes the constraint
// unsatisfied; compilation aborts.
static_assert(consumes_semiring<LatticeOnly>());

int main() { return 0; }
