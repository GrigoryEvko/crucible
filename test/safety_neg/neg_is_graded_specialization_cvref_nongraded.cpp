// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-014 audit fixture: pins cv-ref-stripping symmetry of
// is_graded_specialization_v on the REJECTION side.
//
// Post-A3-014, is_graded_specialization_v applies std::remove_cvref_t
// to T before checking the strict-identity specialization rule.
// Symmetric with IsGraded<T> (Graded.h line 1240).  This fixture
// pins the symmetry by asserting that a cv-ref-qualified non-Graded
// type (`int const&`) is STILL rejected — the cv-ref strip widens
// the admission only for types whose unqualified form IS Graded<...>.
// Non-Graded types stay rejected regardless of qualifier.
//
// If a future refactor strips the wrong direction (e.g., the trait
// gains `requires (is_lvalue_reference_v<T>)` and admits int&), the
// gate silently admits arbitrary references — this fixture fires
// loudly instead.
//
// Companion to neg_is_graded_specialization_lookalike.cpp: that
// fixture pins strict-identity against structural mimics; this one
// pins it against cv-ref qualified non-Graded types.
//
// Expected diagnostic: "static assertion failed" / "static_assert".

#include <crucible/algebra/GradedTrait.h>

namespace alg = crucible::algebra;

int main() {
    // Asserts the gate ADMITS `int const&` — it must NOT.  Same
    // failure surface as the lookalike fixture: the user-side
    // static_assert fires, proving the rejection path is reachable
    // through the cv-ref-stripped lookup.
    static_assert(alg::is_graded_specialization_v<int const&>,
        "fixy-A3-014: int const& is not a Graded<...> specialization "
        "after cv-ref strip; the symmetry rule must reject it, NOT "
        "admit it for being qualified.");
    return 0;
}
