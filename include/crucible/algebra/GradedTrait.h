#pragma once

// ── crucible::algebra::GradedWrapper concept ────────────────────────
//
// Structural predicate on safety-tree wrappers (Linear, Refined,
// Tagged, Secret, Monotonic, AppendOnly, Stale, TimeOrdered,
// SealedRefined, SharedPermission, …) asserting that the wrapper
// follows the Graded foundation refactor's calling convention
// (25_04_2026.md §2).
//
//   Axiom coverage: TypeSafe — concept rejection is a clean compile-
//                   time diagnostic at every "wrapper-shaped" use site.
//   Runtime cost:   zero — purely consteval / decltype.
//
// ── What the concept asserts ────────────────────────────────────────
//
// A type W satisfies `GradedWrapper` iff ALL of the following hold:
//
//   1. `typename W::value_type`        is well-formed (the wrapped T)
//   2. `typename W::graded_type`       is well-formed (the substrate
//                                                     Graded specialization)
//   3. `W::value_type_name()`          is callable as a consteval
//                                      string_view (forwarder from
//                                      Graded::value_type_name)
//   4. `W::lattice_name()`             is callable as a consteval
//                                      string_view (forwarder from
//                                      Graded::lattice_name)
//
// Every migrated wrapper across safety/ + permissions/ satisfies this
// concept; the test_migration_verification harness folds it over a
// wrapper pack to assert uniformity in one go.  GRADED-NEG-1 (#500)
// adds neg-compile counterexamples that prove the concept REJECTS
// non-conforming types (bare T, partially-migrated wrappers missing a
// forwarder).
//
// ── What the concept does NOT assert ────────────────────────────────
//
// - Sizeof relation.  Different wrapper regimes have different
//   sizeof contracts:
//     regime-1 (Linear/Refined/Tagged/Secret/SealedRefined): sizeof(W)
//                                                            == sizeof(T)
//     regime-2 (Monotonic):                                  sizeof(W)
//                                                            == sizeof(T)
//     regime-3 (AppendOnly):                                 sizeof(W)
//                                                            == sizeof(Storage<T>)
//     regime-4 (Stale/TimeOrdered):                          sizeof(W)
//                                                            == sizeof(T) + sizeof(grade)
//     regime-5 (SharedPermission):                           sizeof(W) == 1
//                                                            (proof token)
//   A blanket sizeof check would either over-constrain (rejecting
//   regime-3..5) or under-constrain (admitting bogus wrappers).  Per-
//   regime sizeof witnesses live in test_migration_verification.cpp.
//
// - Public copy/move/swap signatures.  Some wrappers (Linear, Secret)
//   delete copy intentionally; others (Tagged, Refined) default it.
//   Asserting either would over-constrain.
//
// - Construction surface.  Wrappers vary widely (T-only, in_place,
//   Trusted-tag, conversion-from-Refined, etc.).  No uniform
//   constructor signature exists.
//
// The concept asserts the MINIMUM uniformity that downstream
// diagnostic infrastructure (mCRL2 export, future GradedWrapper-
// generic algorithms, the GradedWrapper-folded
// test_migration_verification check) requires.

#include <crucible/algebra/Graded.h>

#include <concepts>
#include <string_view>
#include <type_traits>

namespace crucible::algebra {

// ── is_graded_wrapper — boolean trait (variable template form) ─────
//
// Defaults to false.  Every conforming wrapper specializes this to
// true via the GradedWrapper concept satisfaction below.  The trait
// form is useful for compile-time predicates that need a value
// (e.g., template metafunctions that branch on wrapper-vs-bare).

template <typename W>
inline constexpr bool is_graded_wrapper_v = false;

// ── GradedWrapper concept ──────────────────────────────────────────

template <typename W>
concept GradedWrapper = requires {
    // Public typedefs.
    typename W::value_type;
    typename W::graded_type;

    // Diagnostic forwarders — must be consteval, must return
    // string_view.  The requires-expression instantiates them in
    // unevaluated context; if the forwarder is missing or wrong
    // return type, the concept fails cleanly.
    { W::value_type_name() } -> std::same_as<std::string_view>;
    { W::lattice_name()    } -> std::same_as<std::string_view>;
};

// ── Auto-specialize is_graded_wrapper_v from the concept ──────────
//
// Any type satisfying the concept gets the trait set to true via
// this partial specialization.  Avoids per-wrapper trait
// specializations — a single rule serves the whole family.

template <typename W>
    requires GradedWrapper<W>
inline constexpr bool is_graded_wrapper_v<W> = true;

}  // namespace crucible::algebra
