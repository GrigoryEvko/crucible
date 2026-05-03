// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Phase 0 P0-6 fixture (#1099) for safety::DimensionTraits.h's
// TypestateGrade concept (#1094).
//
// Why this matters: Tier-T dimensions (DimensionAxis::Protocol —
// the only Tier-T dimension per fixy.md §24.1 — captures session-
// protocol typestate) compose via state TRANSITIONS, not via
// lattice join/meet.  A grade type used at a Tier-T site MUST
// expose `state_type` and `transition_type` so the substrate's
// session-typing machinery (sessions/Session.h) can drive the
// state transitions structurally.
//
// A lattice grade (TestLattice in DimensionTraits' self-test
// fixture, which has element_type + bottom/top/leq/join/meet) is
// NOT a typestate.  Lattices model ordered values; typestates
// model legal-transition graphs.  The two are disjoint algebraic
// structures despite both being "discrete state spaces" in
// English.
//
// Without the TypestateGrade concept gate, a maintainer who tags
// a session-protocol type with the lattice shape (or vice versa)
// would get downstream "no member named 'state_type'" errors
// from the session machinery — confusing because the surface
// shape (an enum-like discrete structure) looks plausible.  This
// fixture catches the mistake at the concept gate.
//
// Expected diagnostic: "constraints not satisfied" pointing at
// the TypestateGrade<TestLattice> evaluation.

#include <crucible/safety/DimensionTraits.h>

namespace neg = crucible::safety;

// TestLattice from the DimensionTraits self-test — has element_type
// + bottom/top/leq/join/meet but NO state_type / transition_type.
// Reproduced here so the fixture is self-contained (the
// detail::dimension_traits_self_test namespace is internal).
struct TestLattice {
    using element_type = bool;
    [[nodiscard]] static constexpr bool bottom() noexcept { return false; }
    [[nodiscard]] static constexpr bool top()    noexcept { return true;  }
    [[nodiscard]] static constexpr bool leq (bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
};

// Bridge fires: TypestateGrade<TestLattice> requires `state_type`
// and `transition_type` member typedefs.  TestLattice has neither
// (it carries lattice-shape primitives, not typestate-shape).
template <neg::TypestateGrade G>
constexpr bool consumes_typestate() noexcept { return true; }

[[maybe_unused]] constexpr bool the_fixture =
    consumes_typestate<TestLattice>();

int main() { return 0; }
