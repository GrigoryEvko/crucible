// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-014 audit fixture: pins the cv-ref-symmetric specialization
// gate against a "Graded-lookalike" that mimics the diagnostic surface
// of a real Graded<M, L, T> but is NOT itself a Graded<...>
// specialization.
//
// is_graded_specialization_v<T> is strict-identity: it answers
// "is T literally a Graded<M, L, T> specialization?", NOT "does T
// quack like a GradedWrapper?".  The lookalike struct below has
// matching public typedefs (value_type, lattice_type, graded_type)
// + matching forwarders (value_type_name, lattice_name) — every
// surface property GradedWrapper checks — but is not a Graded<...>
// instance.  is_graded_specialization_v MUST return false; the
// static_assert asserting the opposite MUST fail to compile.
//
// Closes the same root pattern as A3-004 (IsExecCtx asymmetry):
// concept-family gates that admit "structurally similar" types when
// they should demand "exact identity" are silent over-acceptance
// bugs.  This fixture proves the gate doesn't drift.
//
// Expected diagnostic: "static assertion failed" / "static_assert"
// (the gate correctly returns false, fires the user-side assertion).

#include <crucible/algebra/GradedTrait.h>
#include <crucible/algebra/Lattice.h>

#include <string_view>

namespace alg = crucible::algebra;

// Lookalike: exposes every accessor GradedWrapper inspects, but is
// not Graded<M, L, T>.  is_graded_specialization_v MUST reject.
struct GradedLookalike {
    using value_type   = int;
    using lattice_type = alg::detail::lattice_self_test::TrivialBoolLattice;
    using graded_type  = GradedLookalike;  // self-pointing — not a Graded<...>
    static consteval std::string_view value_type_name() { return "int"; }
    static consteval std::string_view lattice_name()    { return "Trivial"; }
    static constexpr alg::ModalityKind modality = alg::ModalityKind::Absolute;
};

int main() {
    // Asserts the gate ADMITS the lookalike — it must NOT, so the
    // static_assert MUST fail to compile.  If a refactor drops the
    // strict-identity rule and the trait fires `true` on the
    // lookalike, this fixture compiles silently and the fixy-A3-014
    // foreclosure regresses.
    static_assert(alg::is_graded_specialization_v<GradedLookalike>,
        "fixy-A3-014: this MUST be false — GradedLookalike is not a "
        "Graded<...> specialization; if it admits, strict-identity "
        "rule has drifted to structural-match");
    return 0;
}
