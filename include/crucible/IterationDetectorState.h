#pragma once

// ── IterationDetector typestate (WRAP-IterDet-4 / #930) ────────────
//
// Codifies IterationDetector's two structural phases as ScopedView
// tags so callers can witness state at compile time:
//
//   iter_det_state::Building  ⟺  signature_len.get() <  K   (phase 1)
//   iter_det_state::Steady    ⟺  signature_len.get() == K   (phase 2)
//
// The two states are mutually exclusive and exhaustive — every reachable
// IterationDetector value is in exactly one of them.  Building is the
// signature-collection phase (build_signature_ feeds at most K ops);
// Steady is the sequential-matcher phase, where every check() is a
// straight expected-hash comparison.  reset() rewinds to Building from
// any state via std::construct_at on the (Bounded)Monotonic counters
// (see IterationDetector.h:204 reset()'s doc-comment).
//
// ── Why a sibling header (and not in IterationDetector.h directly) ──
//
// IterationDetector.h is hot-path-adjacent: it gets included from the
// BG drain pipeline (BackgroundThread.h), the hot-loop benchmarks,
// and several tests.  ScopedView.h pulls in <meta>, <vector>,
// <optional>, <variant>, <array>, <tuple>, <memory> for the Tier-2
// reflection storage audit — a heavier transitive footprint than
// IterationDetector.h's existing slim include set (Platform / Saturate
// / Types / Mutation / Pre / Refined / <memory>).  Splitting the
// typestate codification keeps the hot path lean: callers that want
// a typestate proof include IterationDetectorState.h; everyone else
// pays nothing.
//
// ── Use site ──
//
// At a control-flow point that has just observed signature_len == K
// (typically: after K successful build_signature_ writes, or after
// reset-then-K-feeds), the caller may mint a Steady view:
//
//   auto sv = ::crucible::safety::mint_view<
//       ::crucible::iter_det_state::Steady>(detector);
//   // sv now witnesses Steady; downstream functions taking
//   //   ScopedView<IterationDetector, Steady>
//   // accept it without re-checking.
//
// The mint_view call carries a `pre(view_ok(...))` contract — under
// semantic=enforce a wrong-state mint aborts; under semantic=ignore
// it folds to [[assume]]; under consteval (constant evaluation) it
// becomes ill-formed per P1494R5.  The two HS14 fixtures
// neg_iter_det_view_steady_on_building.cpp and
// neg_iter_det_view_in_field.cpp pin the gate.
//
// ── Axiom coverage ──
//
//   TypeSafe — wrong-tag witness at a function boundary is a
//              compile error (different ScopedView<C, T> types).
//   InitSafe — view_ok overloads read the same fields the existing
//              CRUCIBLE_POST clauses witness on reset() (see
//              IterationDetector.h:240-245).  Read-only.
//   MemSafe  — ScopedView is non-owning; sizeof == sizeof(void*),
//              constructor takes Carrier const& tagged
//              CRUCIBLE_LIFETIMEBOUND.  Tier-2 reflection audit
//              forbids storing the view in a struct field (locked
//              below by static_assert).
//
// ── Runtime cost ──
//
// Tag types are empty (Regime-1 EBO collapse).  view_ok is
// constexpr `[[nodiscard]] bool` — at -O3 the call inlines into a
// single uint32_t comparison against IterationDetector::K (a
// compile-time constant).  ScopedView<IterationDetector, T> is
// sizeof(void*); the tag carries no storage.  Construction pays
// one runtime `view_ok` check; downstream consumers pay zero.

#include <crucible/IterationDetector.h>
#include <crucible/safety/ScopedView.h>

#include <type_traits>

namespace crucible {

// ── Typestate tags ─────────────────────────────────────────────────
//
// Empty class types (zero storage) discriminating IterationDetector's
// two structural phases at the type level.  Lives in a sub-namespace
// so call sites read `iter_det_state::Steady` (the "what" is in the
// tag, the "where" is in the namespace) and the tags don't collide
// with similar Building/Steady tags other carriers may want.

namespace iter_det_state {

// Phase 1 of check(): the detector is still collecting the K-op
// signature.  signature_len.get() < K.  Newly default-constructed
// IterationDetector instances and detectors immediately after
// reset() are in this state.
struct Building {};

// Phase 2 of check(): the K-op signature is locked and the
// sequential matcher is running.  signature_len.get() == K.
// Reaching this state requires K successful build_signature_
// writes; once entered, the only way back to Building is reset().
struct Steady   {};

} // namespace iter_det_state

// ── view_ok ADL hooks ──────────────────────────────────────────────
//
// crucible::safety::mint_view<Tag>(carrier) calls
// `view_ok(carrier, std::type_identity<Tag>{})` as a contract
// precondition.  ADL finds these overloads from `crucible` (the
// IterationDetector type's namespace).  Both overloads are
// constexpr-capable so a Building-state mint of a Steady view at
// constant evaluation is ill-formed (P1494R5 — pre violation in
// constexpr context makes the result non-constant).

[[nodiscard]] constexpr bool
view_ok(IterationDetector const& detector,
        std::type_identity<iter_det_state::Building>) noexcept {
    return detector.signature_len.get() < IterationDetector::K;
}

[[nodiscard]] constexpr bool
view_ok(IterationDetector const& detector,
        std::type_identity<iter_det_state::Steady>) noexcept {
    return detector.signature_len.get() == IterationDetector::K;
}

// ── Tier-2 reflection audit ────────────────────────────────────────
//
// Locks: no field of IterationDetector is itself a ScopedView<...>.
// If a future refactor adds (say) a cached `ScopedView<Detector,
// Building> last_state_` field, this static_assert fires at compile
// time naming the offending type — defeating the lifetime contract
// that ScopedView<C, T> witnesses must not outlive their carrier.
// The audit walks IterationDetector's nonstatic data members + known
// container wrappers (optional/vector/array/pair/tuple/variant) per
// safety/ScopedView.h's contains_scoped_view recursion.

static_assert(::crucible::safety::no_scoped_view_field_check<IterationDetector>(),
    "IterationDetector must not contain a safety::ScopedView field "
    "— views are non-owning lifetime-bounded witnesses; storing one "
    "as a member defeats the lifetime contract.  See "
    "include/crucible/safety/ScopedView.h discipline (Tier 2).");

} // namespace crucible
