#pragma once
// fixy::wrap::Refined granular surface.
//
// Foundational headers (Types.h and peers) need only the Refined family
// — Refined<Pred, T> plus its predicates and combinator — NOT the full
// fixy/Wrap.h umbrella.  The umbrella re-exports every safety wrapper,
// including OwnedRegion → Arena → LifetimeLattice; dragging that into the
// near-universal Types.h regressed the SessionPersistence.h dep-edge
// guard (session_persistence_include_audit, fixy-A2-014): Types.h is
// transitively pulled by SessionPersistence.h, so Types.h → fixy/Wrap.h →
// OwnedRegion.h → Arena.h re-introduced the heavy transitive set the
// guard exists to keep out.
//
// This granular header surfaces the Refined family through fixy::wrap::
// at zero Arena cost — safety/Refined.h + safety/RefinedAlgebra.h pull
// only Graded + BoolLattice + Linear (verified Arena-free).  fixy/Wrap.h
// includes this header so the umbrella still surfaces the full family
// (single source of truth); light consumers include THIS header directly.

#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>   // all_of predicate combinator

#include <type_traits>                        // FIXY-U-115 self-test sentinel

namespace crucible::fixy::wrap {

// Refined<Pred, T> — predicate-checked at construction.
using ::crucible::safety::Refined;
// Named refinement aliases — load-bearing per CLAUDE.md §XVI.
using ::crucible::safety::NonNull;
using ::crucible::safety::Positive;
using ::crucible::safety::NonNegative;
using ::crucible::safety::PowerOfTwo;
// FIXY-U-159 — three §XVI alias-discipline gaps closed:
//   * NonZero<T>      — production sites previously defined inline
//                       (e.g. CallSiteTable.h:114 NonZeroHash).
//   * NonEmpty<T>     — non_empty lambda surface, container-shape.
//   * NonEmptySpan<T> — the §XVI-cited canonical alias for span<T>
//                       with ≥1 element, length_ge<1>-backed so it
//                       composes through predicate_implies subsort.
using ::crucible::safety::NonZero;
using ::crucible::safety::NonEmpty;
using ::crucible::safety::NonEmptySpan;
// FIXY-U-160 — parameterised §XVI alias surface:
//   * MinLength<N, T>    — Refined<length_ge<N>, T>, container min-size
//                          alias used at production sites that want a
//                          grep-target instead of inline length_ge<N>.
//   * MaxBounded<Max, T> — Refined<bounded_above<Max>, T>, the upper-
//                          bound family covering 142+ inline sites
//                          (sat-counter pattern, CKernel-2 et al.).
using ::crucible::safety::MinLength;
using ::crucible::safety::MaxBounded;
// FIXY-U-161 — closes the §XVI parameterised-alias surface:
//   * AlignedTo<N, T>       — Refined<aligned<N>, T>, pointer alignment
//                             (T must be a pointer type per
//                             aligned<N>'s `auto* p` parameter).
//   * WithinRange<L, H, T>  — Refined<in_range<L, H>, T>, closed
//                             interval bound (both endpoints inclusive).
//                             Strictly stricter than MaxBounded — T
//                             must support BOTH operator>= AND <= against
//                             the deduced NTTP types.
using ::crucible::safety::AlignedTo;
using ::crucible::safety::WithinRange;
// Refined composition with Linear (both orderings).
using ::crucible::safety::LinearRefined;
using ::crucible::safety::RefinedLinear;
// Common stateless predicates — usable as Refined NTTP.
using ::crucible::safety::positive;
using ::crucible::safety::non_negative;
using ::crucible::safety::non_zero;
using ::crucible::safety::non_null;
using ::crucible::safety::power_of_two;
using ::crucible::safety::non_empty;
// Predicate combinator (RefinedAlgebra.h) — conjunction of predicates,
// `all_of<power_of_two, bounded_above<128>>` etc.  FIXY-U-096s.
using ::crucible::safety::all_of;
// Parameterised predicate templates.
using ::crucible::safety::Aligned;
using ::crucible::safety::InRange;
using ::crucible::safety::BoundedAbove;
using ::crucible::safety::LengthGe;
using ::crucible::safety::aligned;
using ::crucible::safety::in_range;
using ::crucible::safety::bounded_above;
using ::crucible::safety::length_ge;
// Cross-predicate implication trait — used by SessionPayloadSubsort.
using ::crucible::safety::predicate_implies;
using ::crucible::safety::implies_v;

// mint_refined<Pred, T>(value) — §XXI Universal Mint Pattern (FIXY-U-115).
// Single grep-target for Refined<Pred, T> construction in fixy-only code;
// direct `Refined<Pred, T>{value}` ctor bypasses the §XXI authorization
// surface per fixy-A4-018 precedent (mint_fn direct-ctor dilution).
using ::crucible::safety::mint_refined;

}  // namespace crucible::fixy::wrap

// ── Self-test ──────────────────────────────────────────────────────
//
// Witness that mint_refined re-export resolves to the substrate
// symbol; pattern matches fixy/Wrap.h dual-export sentinels.
namespace crucible::fixy::wrap::self_test {

static_assert(std::is_same_v<
    decltype(&::crucible::safety::mint_refined<::crucible::safety::positive, int>),
    decltype(&::crucible::fixy::wrap::mint_refined<::crucible::safety::positive, int>)>,
    "FIXY-U-115: fixy::wrap::mint_refined must alias safety::mint_refined.");

}  // namespace crucible::fixy::wrap::self_test
