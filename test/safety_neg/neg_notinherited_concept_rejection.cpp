// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a non-final type `T` to a function template
// CONCEPT-CONSTRAINED with `template <crucible::safety::NotInherited
// T> void f()`.  The C++20 concept-not-satisfied rejection path is
// language-level overload resolution — DISTINCT from the consteval
// static_assert path covered by the sibling fixture
// neg_notinherited_non_final.cpp.
//
// Discipline rationale (NotInherited.h:50-77, two distinct rejection
// mechanisms documented inline):
//   The NotInherited primitive ships TWO rejection paths because each
//   serves a different idiom:
//     - WITNESS-AS-CONCEPT (THIS file): a function whose parameter
//       set demands a `NotInherited T`.  Rejection is "constraints
//       not satisfied for ..." at OVERLOAD RESOLUTION time, before
//       the template body even instantiates.  Use this where the
//       constraint is part of the public type signature (cleaner
//       error, no body instantiation needed).
//     - WITNESS-AS-CONSTEVAL (sibling fixture): a function body that
//       calls `assert_not_inherited<T>()`.  Rejection is "static
//       assertion failed: [NotInherited_Not_Final] ..." DURING TEMPLATE
//       BODY INSTANTIATION.  Use this where the constraint applies
//       only under specific template-parameter combinations that the
//       outer signature cannot express.
//
//   HS14 demands a witness for EACH path because a refactor that
//   silently changed `concept NotInherited = std::is_final_v<T>` to
//   `concept NotInherited = true` (or accidentally removed the
//   constraint at a call site) would compile cleanly through the
//   sibling consteval-static-assert fixture — which never exercises
//   the concept path.  Two fixtures, two mechanisms, drift caught on
//   both axes.
//
// HS14 — distinct-class fixture pair for NotInherited:
//   * Class M-consteval (sibling): static_assert inside
//     assert_not_inherited<NonFinal>() fires the
//     [NotInherited_Not_Final] tag at template-body-instantiation.
//   * Class U-concept (THIS file): concept-constrained function
//     template's `template <NotInherited T>` constraint not
//     satisfied at overload-resolution time.
//
// FIXY-U-146 — bumps NotInherited from 1 → 2 fixtures (HS14 floor
// met).  Companion to U-146 FinalBy (deleted-copy-of-protected)
// and U-146 Stale (rvalue-only-consume).

#include <crucible/safety/NotInherited.h>

namespace {
    // Non-final type — fails the std::is_final_v<T> predicate baked
    // into the NotInherited concept.  Production shape: any type
    // intentionally declared without `final` so derivation IS
    // possible (e.g. a base class in an OOP hierarchy that
    // accidentally got passed to a final-only API).
    struct NonFinalType {
        int payload_ = 0;
    };

    // Final type — anchor case.  This compiles cleanly when passed
    // to the same concept-constrained API.  Demonstrates the gate
    // does ADMIT the well-formed input.
    struct FinalType final {
        int payload_ = 0;
    };

    // Concept-constrained API.  In production this would be a
    // registration function, a serialization endpoint, or any API
    // that needs structural assurance the type is a leaf in the
    // inheritance tree (no polymorphic-slicing footgun, no
    // virtual-destructor leak).
    template <::crucible::safety::NotInherited T>
    [[maybe_unused]] void register_final_type() {
        // body irrelevant — concept rejection is the test.
    }
}

// Anchor: concept-satisfied case compiles.  Documents that the
// gate ADMITS final types — proves the test isn't a false positive
// against the entire template.
[[maybe_unused]] static void anchor_register_final_type() {
    register_final_type<FinalType>();
}

// VIOLATION: NonFinalType fails `std::is_final_v<T>` which the
// NotInherited concept (NotInherited.h:93) requires.  GCC emits
// "constraints not satisfied for ..." with the concept's
// expansion site in the diagnostic chain.  DIFFERENT from the
// sibling fixture's "static assertion failed" — same predicate,
// different language-level rejection mechanism.
[[maybe_unused]] static void offending_register_non_final() {
    register_final_type<NonFinalType>();   // ERROR: concept
                                           // NotInherited not
                                           // satisfied for
                                           // NonFinalType
}

int main() { return 0; }
