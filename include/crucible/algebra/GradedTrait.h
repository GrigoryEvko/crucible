#pragma once

// ── crucible::algebra::GradedWrapper concept ────────────────────────
//
// Structural predicate on safety-tree wrappers (Linear, Refined,
// Tagged, Secret, Monotonic, AppendOnly, Stale, TimeOrdered,
// SealedRefined, SharedPermission, …) asserting that the wrapper
// follows the Graded foundation refactor's calling convention
// (25_04_2026.md §2).
//
// ── Wrapper regime taxonomy (DOC-REGIME-CLASSIFICATION) ────────────
//
// Migrated wrappers fall into FIVE regimes, distinguished by how
// the substrate's grade (the lattice element) relates to runtime
// storage.  The regime determines what sizeof witnesses are
// admissible per wrapper; the GradedWrapper concept itself is
// regime-blind — it asserts the diagnostic surface uniformity that
// every regime shares.
//
// regime-1 — Zero-cost EBO collapse (the canonical case)
//     The lattice's element_type is empty (singleton grade).
//     Graded's [[no_unique_address]] grade_ collapses to 0 bytes;
//     sizeof(W) == sizeof(T).
//     Members: Linear, Refined, Tagged, Secret, SealedRefined.
//
// regime-2 — T == element_type collapse
//     The lattice's element_type IS the wrapped type T (e.g.
//     MonotoneLattice<T, Cmp>::element_type == T).  Graded's
//     specialization for value-type-equals-grade-type collapses
//     value+grade into one storage cell; sizeof(W) == sizeof(T).
//     Members: Monotonic.
//
// regime-3 — Derived grade from container content
//     The grade is computed on demand from the wrapped container's
//     contents (e.g. SeqPrefixLattice grade derived from
//     container.size()).  Storage is sizeof(container); no separate
//     grade field.  sizeof(W) == sizeof(Storage<T>).
//     Members: AppendOnly.
//
// regime-4 — T + grade carried per instance
//     Lattice element is non-trivial (e.g. vector clock with N
//     entries, or staleness counter).  Wrapper carries both T and
//     the grade as separate members; sizeof(W) ==
//     sizeof(T) + sizeof(grade) + alignment.  This is the
//     "first-class graded value" regime.
//     Members: Stale, TimeOrdered.
//
// regime-5 — Proof-token, runtime carrier elsewhere
//     The wrapper is a phantom proof token (sizeof 1 / EBO-
//     collapsible) that asserts a property whose runtime state
//     lives in a separate carrier (Pool, atomic, etc.).  The
//     graded_type alias documents the substrate identity for
//     diagnostic introspection but doesn't carry the grade.
//     Members: SharedPermission (carrier = SharedPermissionPool).
//
// When introducing a new wrapper, identify its regime first; the
// regime determines the sizeof witness shape AND the cross-
// composition properties available.
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

// ── is_graded_specialization — substrate-validity trait ────────────
//
// True iff T is a Graded<M, L, U> specialization for some M, L, U.
// Used by the strengthened GradedWrapper concept (Round-3 audit, gap
// C1) to reject wrappers that expose graded_type but point it at a
// non-Graded type.
//
// Without this, the prior concept admitted:
//
//   struct Bogus {
//       using value_type = int;
//       using graded_type = void;        // <- not actually Graded
//       static consteval std::string_view value_type_name() { ... }
//       static consteval std::string_view lattice_name()    { ... }
//   };
//   static_assert(GradedWrapper<Bogus>);  // PASSED (bug)

template <typename T>
struct is_graded_specialization : std::false_type {};

template <ModalityKind M, typename L, typename T>
struct is_graded_specialization<Graded<M, L, T>> : std::true_type {};

template <typename T>
inline constexpr bool is_graded_specialization_v =
    is_graded_specialization<T>::value;

// ── graded_modality — extract the ModalityKind value from a Graded ──
//
// Round-4 audit, gap CHEAT-5: lets the GradedWrapper concept verify
// that a wrapper's claimed modality matches its substrate's
// modality.  Catches the wrapper-says-Absolute-but-substrate-is-
// Comonad case the prior concept admitted.

template <typename T>
struct graded_modality;

template <ModalityKind M, typename L, typename T>
struct graded_modality<Graded<M, L, T>>
    : std::integral_constant<ModalityKind, M> {};

template <typename T>
inline constexpr ModalityKind graded_modality_v = graded_modality<T>::value;

// ── value_type_decoupled — opt-out for substrate decoupling ────────
//
// Round-4 audit, gap CHEAT-1: by default, GradedWrapper enforces
// `W::value_type == W::graded_type::value_type` (the wrapper's
// user-facing value type matches the substrate's carried type).
// AppendOnly is the canonical exception — its value_type is T
// (the element) while graded_type::value_type is Storage<T>
// (the container being graded by length).  AppendOnly opts out
// of the equality check by specializing this trait to true.
//
// New wrappers that LAYER user semantics over a different substrate
// type (regime-3-style: container substrates with element-typed
// user surface) opt out the same way.  Default false; concept
// enforces equality unless explicit opt-out.

template <typename W>
struct value_type_decoupled : std::false_type {};

template <typename W>
inline constexpr bool value_type_decoupled_v = value_type_decoupled<W>::value;

// ── is_graded_wrapper — boolean trait (variable template form) ─────
//
// Defaults to false.  Every conforming wrapper specializes this to
// true via the GradedWrapper concept satisfaction below.  The trait
// form is useful for compile-time predicates that need a value
// (e.g., template metafunctions that branch on wrapper-vs-bare).

template <typename W>
inline constexpr bool is_graded_wrapper_v = false;

// ── GradedWrapper concept ──────────────────────────────────────────
//
// Round-3 + Round-4 audit strengthening:
//   - C1: graded_type must be a Graded<...> specialization
//   - C2: lattice_type required
//   - L3: forwarders must be noexcept
//   - CHEAT-1: value_type matches graded_type::value_type unless the
//              wrapper opts out via value_type_decoupled<W> (regime-3)
//   - CHEAT-2: lattice_type matches graded_type::lattice_type
//   - CHEAT-3: forwarder fidelity at concept level — value_type_name()
//              and lattice_name() must equal graded_type's forwarders
//   - CHEAT-5: modality match — wrapper's `static constexpr modality`
//              field matches graded_type's modality template parameter
//
// Adversarial probe coverage at test/test_concept_cheat_probe.cpp.
//
// L2 strict-equality (value_type ↔ graded_type::value_type for ALL
// wrappers) was rejected because AppendOnly<T, Storage> legitimately
// has `value_type = T` and `graded_type::value_type = Storage<T>`.
// Round-4's CHEAT-1 fix is the WEAK form: enforce equality by
// default, allow opt-out via value_type_decoupled<W> trait.

template <typename W>
concept GradedWrapper = requires {
    // Public typedefs.
    typename W::value_type;
    typename W::lattice_type;          // C2: family-wide alias
    typename W::graded_type;

    // C1: graded_type must actually be a Graded<...> specialization,
    // not an arbitrary type that happens to be named graded_type.
    requires is_graded_specialization_v<typename W::graded_type>;

    // CHEAT-2: substrate lattice consistency.  Wrapper's claimed
    // lattice_type must match the substrate's lattice template arg.
    requires std::same_as<typename W::lattice_type,
                          typename W::graded_type::lattice_type>;

    // CHEAT-1: substrate value_type consistency, with opt-out for
    // regime-3 wrappers (AppendOnly's Storage<T> case).  Default is
    // strict equality; wrappers that legitimately decouple
    // user-facing value_type from substrate value_type specialize
    // value_type_decoupled<W> to std::true_type.
    requires (value_type_decoupled_v<W>
              || std::same_as<typename W::value_type,
                              typename W::graded_type::value_type>);

    // CHEAT-5: modality consistency.  Wrapper exposes `static
    // constexpr ModalityKind modality = ...;` declaring its modality;
    // concept verifies it matches the substrate's modality template
    // arg.  Catches wrapper-says-Absolute-but-substrate-is-Comonad
    // bugs that the prior concept admitted.
    requires (W::modality == graded_modality_v<typename W::graded_type>);

    // Diagnostic forwarders — must be noexcept, must return
    // string_view (L3: noexcept enforcement).
    { W::value_type_name() } noexcept -> std::same_as<std::string_view>;
    { W::lattice_name()    } noexcept -> std::same_as<std::string_view>;

    // CHEAT-3: forwarder fidelity at concept level.  The forwarders
    // must return the SAME strings as the substrate's forwarders
    // (otherwise diagnostics emit lying names).  Catches wrappers
    // that ship custom string-returning bodies that don't actually
    // forward.  Both expressions are consteval (forwarders are
    // consteval and substrate's are consteval), so the comparison
    // is a constant expression.
    requires (W::value_type_name() ==
              W::graded_type::value_type_name());
    requires (W::lattice_name() ==
              W::graded_type::lattice_name());
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
