#pragma once

// ── crucible::fixy::modality — Top-level modality surface ──────────
//
// FIXY-U-060.  Surfaces the six ModalityKind values + six per-form
// concepts + six tag types + diagnostic helpers at a top-level
// `fixy::modality::` namespace, complementing the existing
// `fixy::algebra::modality::` path (which goes through Algebra.h).
//
// Per CLAUDE.md §XVI the six modality forms (Comonad, RelativeMonad,
// Absolute, Relative, Quotient, Coeffect) are a fundamental algebraic
// axis of the Graded<M, L, T> substrate; surfacing them at the
// canonical short-name path means callers writing
// `fixy::modality::ComonadModality<K>` or
// `fixy::modality::ModalityKind::Comonad` never have to descend
// through `fixy::algebra::` for a primitive that is itself
// foundational, not algebra-specific.
//
// ── Cross-reference (fixy-A4-011 dual-export pattern) ─────────────
//
// ModalityKind / per-form concepts / tag types are ALSO reachable
// via `fixy::algebra::*` (Algebra.h line 70-103 + line 97 namespace
// alias).  Both paths name the SAME substrate symbols via
// `using ::crucible::algebra::*` — type identity is preserved by
// construction.  This Modality.h header is the "by-axis" carve-out
// at the natural short-name path; Algebra.h is the broader
// algebra-substrate hub.  See fixy/Safety.h doc-block for the
// canonical dual-export discipline.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   algebra::ModalityKind              — 6-value enum class
//   algebra::IsModality<K>             — well-formedness gate
//   algebra::{Comonad,RelativeMonad,Absolute,Relative,Quotient,Coeffect}Modality<K>
//                                      — 6 per-form concepts
//   algebra::modality::{Comonad_t,RelativeMonad_t,Absolute_t,
//                       Relative_t,Quotient_t,Coeffect_t}
//                                      — 6 overload-dispatch tag types
//   algebra::has_counit_v<K>           — comonad query
//   algebra::has_unit_v<K>             — relative-monad query
//   algebra::has_grade_only_v<K>       — value-only query
//   algebra::modality_name(K)          — diagnostic name emitter
//   algebra::modality_kind_count       — reflection-derived cardinality
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — enum values are well-defined per substrate; aliases
//              preserve.
//   TypeSafe — using-declarations preserve concept gates (K is gated
//              by IsModality<K> at concept-instantiation time).
//   NullSafe — value-typed; no pointer surface.
//   MemSafe  — empty tag types (sizeof = 1 by C++ rule); aliases
//              preserve.
//   DetSafe  — pure value primitives; bit-exact across re-export.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  using-declarations are pure name-lookup directives;
// `sizeof(fixy::modality::Comonad_t) == sizeof(algebra::modality::Comonad_t)`
// — both are empty types collapsing to 1 byte.

#include <crucible/algebra/Modality.h>

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace crucible::fixy::modality {

// ── Enum + cardinality + diagnostic ───────────────────────────────

using ::crucible::algebra::ModalityKind;
using ::crucible::algebra::modality_name;
inline constexpr std::size_t modality_kind_count =
    ::crucible::algebra::modality_kind_count;

// ── Well-formedness gate ──────────────────────────────────────────

template <ModalityKind K>
concept IsModality = ::crucible::algebra::IsModality<K>;

// ── Six per-form concepts ─────────────────────────────────────────

template <ModalityKind K>
concept ComonadModality       = ::crucible::algebra::ComonadModality<K>;

template <ModalityKind K>
concept RelativeMonadModality = ::crucible::algebra::RelativeMonadModality<K>;

template <ModalityKind K>
concept AbsoluteModality      = ::crucible::algebra::AbsoluteModality<K>;

template <ModalityKind K>
concept RelativeModality      = ::crucible::algebra::RelativeModality<K>;

template <ModalityKind K>
concept QuotientModality      = ::crucible::algebra::QuotientModality<K>;

template <ModalityKind K>
concept CoeffectModality      = ::crucible::algebra::CoeffectModality<K>;

// ── Compile-time queries ──────────────────────────────────────────

template <ModalityKind K>
inline constexpr bool has_counit_v     = ::crucible::algebra::has_counit_v<K>;
template <ModalityKind K>
inline constexpr bool has_unit_v       = ::crucible::algebra::has_unit_v<K>;
template <ModalityKind K>
inline constexpr bool has_grade_only_v = ::crucible::algebra::has_grade_only_v<K>;

// ── Six overload-dispatch tag types ───────────────────────────────
//
// Same identity as algebra::modality::*_t; sizeof == 1 each.

using Comonad_t       = ::crucible::algebra::modality::Comonad_t;
using RelativeMonad_t = ::crucible::algebra::modality::RelativeMonad_t;
using Absolute_t      = ::crucible::algebra::modality::Absolute_t;
using Relative_t      = ::crucible::algebra::modality::Relative_t;
using Quotient_t      = ::crucible::algebra::modality::Quotient_t;
using Coeffect_t      = ::crucible::algebra::modality::Coeffect_t;

}  // namespace crucible::fixy::modality

// ─── FIXY-U-103 in-header sentinel ─────────────────────────────────
//
// Drift-catch for the 6+1 enum values + 7 concepts (IsModality + 6
// per-form) + 6 tag types + 3 compile-time queries + 2 diagnostic
// helpers (modality_name + modality_kind_count).  Substrate-identity
// witnesses for representative items + cardinality mirror against
// `algebra::modality_kind_count` (reflection-derived, line 86 of
// algebra/Modality.h).

namespace crucible::fixy::modality::self_test {

// ── Enum + cardinality identity ──────────────────────────────────

static_assert(std::is_same_v<
    ::crucible::fixy::modality::ModalityKind,
    ::crucible::algebra::ModalityKind>,
    "fixy::modality::ModalityKind must alias algebra::ModalityKind");

static_assert(::crucible::fixy::modality::ModalityKind::Comonad
              == ::crucible::algebra::ModalityKind::Comonad);
static_assert(::crucible::fixy::modality::ModalityKind::RelativeMonad
              == ::crucible::algebra::ModalityKind::RelativeMonad);
static_assert(::crucible::fixy::modality::ModalityKind::Absolute
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(::crucible::fixy::modality::ModalityKind::Relative
              == ::crucible::algebra::ModalityKind::Relative);
static_assert(::crucible::fixy::modality::ModalityKind::Quotient
              == ::crucible::algebra::ModalityKind::Quotient);
static_assert(::crucible::fixy::modality::ModalityKind::Coeffect
              == ::crucible::algebra::ModalityKind::Coeffect);

// ── Concept identity + behavior ──────────────────────────────────

static_assert(::crucible::fixy::modality::IsModality<
    ::crucible::fixy::modality::ModalityKind::Comonad>);
static_assert(::crucible::fixy::modality::IsModality<
    ::crucible::fixy::modality::ModalityKind::Coeffect>);

static_assert(::crucible::fixy::modality::ComonadModality<
    ::crucible::fixy::modality::ModalityKind::Comonad>);
static_assert(!::crucible::fixy::modality::ComonadModality<
    ::crucible::fixy::modality::ModalityKind::RelativeMonad>);

static_assert(::crucible::fixy::modality::CoeffectModality<
    ::crucible::fixy::modality::ModalityKind::Coeffect>);
static_assert(!::crucible::fixy::modality::CoeffectModality<
    ::crucible::fixy::modality::ModalityKind::Quotient>);

// ── Query-fn behavior ────────────────────────────────────────────

static_assert(::crucible::fixy::modality::has_counit_v<
    ::crucible::fixy::modality::ModalityKind::Comonad>);
static_assert(::crucible::fixy::modality::has_unit_v<
    ::crucible::fixy::modality::ModalityKind::RelativeMonad>);
static_assert(::crucible::fixy::modality::has_grade_only_v<
    ::crucible::fixy::modality::ModalityKind::Absolute>);
static_assert(!::crucible::fixy::modality::has_counit_v<
    ::crucible::fixy::modality::ModalityKind::Absolute>);

// ── Tag-type identity ────────────────────────────────────────────

static_assert(std::is_same_v<
    ::crucible::fixy::modality::Comonad_t,
    ::crucible::algebra::modality::Comonad_t>);
static_assert(std::is_same_v<
    ::crucible::fixy::modality::Coeffect_t,
    ::crucible::algebra::modality::Coeffect_t>);

// Each tag type round-trips to its enum kind.
static_assert(::crucible::fixy::modality::Comonad_t::kind
              == ::crucible::fixy::modality::ModalityKind::Comonad);
static_assert(::crucible::fixy::modality::Quotient_t::kind
              == ::crucible::fixy::modality::ModalityKind::Quotient);

// Empty tag types collapse to sizeof == 1.
static_assert(sizeof(::crucible::fixy::modality::Comonad_t)       == 1);
static_assert(sizeof(::crucible::fixy::modality::RelativeMonad_t) == 1);
static_assert(sizeof(::crucible::fixy::modality::Absolute_t)      == 1);
static_assert(sizeof(::crucible::fixy::modality::Relative_t)      == 1);
static_assert(sizeof(::crucible::fixy::modality::Quotient_t)      == 1);
static_assert(sizeof(::crucible::fixy::modality::Coeffect_t)      == 1);

// ── Diagnostic-name reach ────────────────────────────────────────

static_assert(::crucible::fixy::modality::modality_name(
    ::crucible::fixy::modality::ModalityKind::Comonad) == "Comonad");
static_assert(::crucible::fixy::modality::modality_name(
    ::crucible::fixy::modality::ModalityKind::Coeffect) == "Coeffect");

// ── Cardinality mirror — drives off substrate's reflection ───────
//
// FIXY-U-127 / U-128 / U-129 / U-130 floor-vs-ceiling split: the
// EXACT ceiling pin (`== 6`) lives in algebra/Modality.h:181
// colocated with the source-of-truth `modality_kind_count` constant
// (reflection-derived from the substrate ModalityKind enum); THIS
// fixy-side header holds only the FLOOR pin (`>= 6`) catching the
// inverse direction — a ModalityKind enumerator removed from
// substrate.  The structural-identity cross-check above (fixy ==
// algebra value) stays exact since both are aliases of the same
// reflection result.

static_assert(::crucible::fixy::modality::modality_kind_count
              == ::crucible::algebra::modality_kind_count);
static_assert(::crucible::fixy::modality::modality_kind_count >= 6,
    "fixy::modality::modality_kind_count floor: regressed below 6 — "
    "a ModalityKind enumerator was removed from algebra/Modality.h "
    "without updating both the colocated ceiling pin AND this floor "
    "witness.");

constexpr int u060_concept_cardinality  = 7;  // IsModality + 6 per-form
constexpr int u060_tag_cardinality      = 6;  // Comonad_t..Coeffect_t
constexpr int u060_query_cardinality    = 3;  // has_counit/unit/grade_only_v

static_assert(u060_concept_cardinality == 7,
    "fixy::modality:: concept surface drifted from 7 (IsModality + 6 per-form).");
static_assert(u060_tag_cardinality     == 6,
    "fixy::modality:: tag-type surface drifted from 6.");
static_assert(u060_query_cardinality   == 3,
    "fixy::modality:: query-fn surface drifted from 3 (has_*_v family).");

}  // namespace crucible::fixy::modality::self_test
