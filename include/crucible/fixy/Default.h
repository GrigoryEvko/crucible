#pragma once

// ── crucible::fixy — Default.h — per-dim strict defaults ───────────
//
// Phase A of the clean reimplementation per misc/16_05_2026_fixy.md §4.
//
// THIS HEADER MUST NOT DEFINE STRICT DEFAULTS LOCALLY.  The 20-axis
// strict defaults are owned by `safety/Fn.h::Fn<Type, ...>`'s template
// parameter defaults (P0-1, shipped via #1095).  This header is a
// per-dim projection that maps `DimensionAxis::X` → the default value
// or type used by `Fn<Type, ...>` on that axis.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::fn::Fn<int>           — the canonical default instantiation
//   safety::fn::pred::True        — Refinement (dim 2) strict default
//   safety::fn::UsageMode::Linear — Usage (dim 3) strict default
//   safety::effects::Row<>        — Effect (dim 4) strict default
//   safety::fn::SecLevel::Classified — Security (dim 5) strict default
//   safety::fn::proto::None       — Protocol (dim 6) strict default
//   safety::fn::lifetime::Static  — Lifetime (dim 7) strict default
//   safety::source::FromInternal  — Provenance (dim 8) strict default
//   safety::trust::Verified       — Trust (dim 9) strict default
//   safety::fn::ReprKind::Opaque  — Representation (dim 10) strict default
//   safety::fn::cost::Unstated    — Complexity (dim 13) strict default
//   safety::fn::precision::Exact  — Precision (dim 14) strict default
//   safety::fn::space::Zero       — Space (dim 15) strict default
//   safety::fn::OverflowMode::Trap — Overflow (dim 16) strict default
//   safety::fn::MutationMode::Immutable — Mutation (dim 18) strict default
//   safety::fn::ReentrancyMode::NonReentrant — Reentrancy (dim 19)
//   safety::fn::size_pol::Unstated — Size (dim 20) strict default
//   safety::fn::stale::Fresh      — Staleness (dim 22) strict default
//
// 18 of 20 axes carry a strict default at the substrate level.  Two
// axes are caller-supplied:
//
//   DimensionAxis::Type      — there is no "default function type";
//                              every binding names its own type.
//   DimensionAxis::Version   — Fn<>'s u32 default is 1, which IS the
//                              strict default; we project it as a
//                              value here.
//
// (Observability — dim 11 in FX — is DERIVED from the EffectRow per
//  fixy.md §24.1; it has no slot in the substrate's Fn<...> template
//  pack, so it does not appear in `strict_default_for` either.)
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Every `strict_default_for<D>` specialization aliases an
// existing substrate name.  If a future P-row adds a 21st dimension
// to safety::DimensionAxis, this header MUST grow one new
// specialization that aliases the substrate's new default.
//
// ── Why this header exists at all ──────────────────────────────────
//
// Phase B (fixy/Resolve.h) needs a per-dim metafunction that maps
// `DimensionAxis::X` to "the value or type that `Fn<>` would carry
// on axis X if the user wrote `Grant::accept_default_strict_for<X>`".
// Without this projection, Resolve.h would need to switch on
// DimensionAxis at every per-axis slot — bloat that defeats the
// "one source of truth" discipline.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   TypeSafe — every specialization aliases a substrate type or value;
//              no parallel definitions to drift.
//   InitSafe — no fields, no state.
//   DetSafe  — every member is constexpr.
//
// ── Runtime cost ───────────────────────────────────────────────────
//
// Zero.  All members are constexpr; consumers (Resolve.h) compile to
// the substrate's default values directly.
//
// ── Self-test ──────────────────────────────────────────────────────
//
// Two assertions ride this header:
//   1. Every DimensionAxis enumerator must have a strict_default_for
//      specialization (reflection-driven coverage).
//   2. The aliased values + types must round-trip against
//      safety::fn::Fn<int>'s defaults (so when the substrate's
//      defaults change, this header fires a clear error rather than
//      silently drifting).

#include <crucible/fixy/Dim.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/Tagged.h>
#include <crucible/effects/EffectRow.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <type_traits>

namespace crucible::fixy {

// ─── strict_default_for<D> — primary template (intentionally empty) ─
//
// Per-dim specializations below.  An un-specialized invocation fires
// `value_or_type` as a substitution failure, surfaced by the
// reflection-driven coverage check at the bottom of this header.

template <dim::DimensionAxis D>
struct strict_default_for;

// ─── Per-dim specializations ───────────────────────────────────────
//
// Each specialization aliases the substrate's existing default; no
// independent definition.  Members:
//
//   `type`      — for type-valued axes (Refinement / Effect / etc.)
//   `value_type` — the enum type for enum-valued axes
//   `value`     — the strict default value (for enum-valued axes)
//
// A given specialization exposes either `type` OR `(value_type, value)`,
// not both, matching the substrate's axis classification.

template <>
struct strict_default_for<dim::DimensionAxis::Type> {
    // Caller-supplied; no strict default.  Engagement on this axis
    // requires the caller to name the Type explicitly via
    // `fixy::fn<Type, ...>`.  No `type` / `value` member.
    static constexpr bool caller_supplied = true;
};

template <>
struct strict_default_for<dim::DimensionAxis::Refinement> {
    using type = safety::fn::pred::True;
};

template <>
struct strict_default_for<dim::DimensionAxis::Usage> {
    using value_type = safety::fn::UsageMode;
    static constexpr value_type value = safety::fn::UsageMode::Linear;
};

template <>
struct strict_default_for<dim::DimensionAxis::Effect> {
    using type = effects::Row<>;
};

template <>
struct strict_default_for<dim::DimensionAxis::Security> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Classified;
};

template <>
struct strict_default_for<dim::DimensionAxis::Protocol> {
    using type = safety::fn::proto::None;
};

template <>
struct strict_default_for<dim::DimensionAxis::Lifetime> {
    using type = safety::fn::lifetime::Static;
};

template <>
struct strict_default_for<dim::DimensionAxis::Provenance> {
    using type = safety::source::FromInternal;
};

template <>
struct strict_default_for<dim::DimensionAxis::Trust> {
    using type = safety::trust::Verified;
};

template <>
struct strict_default_for<dim::DimensionAxis::Representation> {
    using value_type = safety::fn::ReprKind;
    static constexpr value_type value = safety::fn::ReprKind::Opaque;
};

template <>
struct strict_default_for<dim::DimensionAxis::Observability> {
    // Derived from EffectRow per fixy.md §24.1; the strict default
    // (empty row → no CT discipline) follows from Effect's strict
    // default `Row<>`.  No independent slot in the Fn<...> pack.
    using derived_from = strict_default_for<dim::DimensionAxis::Effect>;
};

template <>
struct strict_default_for<dim::DimensionAxis::Complexity> {
    using type = safety::fn::cost::Unstated;
};

template <>
struct strict_default_for<dim::DimensionAxis::Precision> {
    using type = safety::fn::precision::Exact;
};

template <>
struct strict_default_for<dim::DimensionAxis::Space> {
    using type = safety::fn::space::Zero;
};

template <>
struct strict_default_for<dim::DimensionAxis::Overflow> {
    using value_type = safety::fn::OverflowMode;
    static constexpr value_type value = safety::fn::OverflowMode::Trap;
};

template <>
struct strict_default_for<dim::DimensionAxis::Mutation> {
    using value_type = safety::fn::MutationMode;
    static constexpr value_type value = safety::fn::MutationMode::Immutable;
};

template <>
struct strict_default_for<dim::DimensionAxis::Reentrancy> {
    using value_type = safety::fn::ReentrancyMode;
    static constexpr value_type value = safety::fn::ReentrancyMode::NonReentrant;
};

template <>
struct strict_default_for<dim::DimensionAxis::Size> {
    using type = safety::fn::size_pol::Unstated;
};

template <>
struct strict_default_for<dim::DimensionAxis::Version> {
    using value_type = std::uint32_t;
    static constexpr value_type value = 1u;
};

template <>
struct strict_default_for<dim::DimensionAxis::Staleness> {
    using type = safety::fn::stale::Fresh;
};

// ─── has_strict_default — predicate concept ────────────────────────
//
// True iff a specialization exists for D AND it exposes either `type`
// or `(value_type, value)` (i.e., it isn't the Type-axis "caller-
// supplied" sentinel).  Used by Reject.h to decide whether
// `accept_default_strict_for<Type>` is a coherent grant (it isn't —
// callers must NAME the type, not "accept" it).

template <dim::DimensionAxis D>
concept HasStrictDefault =
    requires { typename strict_default_for<D>::type; }
    || requires {
        typename strict_default_for<D>::value_type;
        { strict_default_for<D>::value };
    };

// ─── HasDerivedDefault — Observability is the only derived axis ────
//
// True iff the dim's strict default is computed from another dim
// (Observability ← Effect).  Reject.h treats `accept_default` on a
// derived axis the same as a primary axis — it's the author saying
// "I accept whatever the upstream dim resolves to."

template <dim::DimensionAxis D>
concept HasDerivedDefault =
    requires { typename strict_default_for<D>::derived_from; };

template <dim::DimensionAxis D>
concept IsCallerSupplied =
    requires { { strict_default_for<D>::caller_supplied } -> std::convertible_to<bool>; }
    && strict_default_for<D>::caller_supplied;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test — reflection-driven coverage ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Iterate every DimensionAxis enumerator, verify EXACTLY ONE of the
// three concept slots is satisfied: caller-supplied, has-strict-default,
// or has-derived-default.  An un-specialized dim fails all three and
// the assertion fires with the offending enumerator name.

namespace detail::default_coverage {

[[nodiscard]] consteval bool every_axis_resolves() noexcept {
    static constexpr auto resolve_axes = std::define_static_array(
        std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : resolve_axes) {
        constexpr auto axis_v = [:en:];
        constexpr bool has_caller    = IsCallerSupplied<axis_v>;
        constexpr bool has_strict    = HasStrictDefault<axis_v>;
        constexpr bool has_derived   = HasDerivedDefault<axis_v>;
        constexpr int  match_count   = (has_caller ? 1 : 0)
                                     + (has_strict ? 1 : 0)
                                     + (has_derived ? 1 : 0);
        if (match_count != 1) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// Round-trip self-test: the type-valued axes' aliases must match
// safety::fn::Fn<int>'s default member types.  If the substrate
// changes (e.g., Fn<>'s Refinement default becomes something other
// than pred::True), this fires the regression at fixy/Default.h
// rather than at an opaque Resolve.h substitution failure.

[[nodiscard]] consteval bool type_defaults_match_substrate() noexcept {
    using DF = safety::fn::Fn<int>;
    return  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Refinement>::type, DF::refinement_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Effect>::type,     DF::effect_row_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Protocol>::type,   DF::protocol_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Lifetime>::type,   DF::lifetime_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Provenance>::type, DF::source_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Trust>::type,      DF::trust_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Complexity>::type, DF::cost_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Precision>::type,  DF::precision_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Space>::type,      DF::space_t>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Size>::type,       DF::size_t_>
        &&  std::is_same_v<typename strict_default_for<dim::DimensionAxis::Staleness>::type,  DF::staleness_t>;
}

[[nodiscard]] consteval bool enum_defaults_match_substrate() noexcept {
    using DF = safety::fn::Fn<int>;
    return  strict_default_for<dim::DimensionAxis::Usage>::value          == DF::usage_v
        &&  strict_default_for<dim::DimensionAxis::Security>::value       == DF::security_v
        &&  strict_default_for<dim::DimensionAxis::Representation>::value == DF::repr_v
        &&  strict_default_for<dim::DimensionAxis::Overflow>::value       == DF::overflow_v
        &&  strict_default_for<dim::DimensionAxis::Mutation>::value       == DF::mutation_v
        &&  strict_default_for<dim::DimensionAxis::Reentrancy>::value     == DF::reentrancy_v
        &&  strict_default_for<dim::DimensionAxis::Version>::value        == DF::version_v;
}

}  // namespace detail::default_coverage

static_assert(detail::default_coverage::every_axis_resolves(),
    "fixy::Default — at least one DimensionAxis enumerator does not "
    "have a strict_default_for specialization (or has multiple "
    "conflicting role markers).  Each axis must classify as exactly "
    "ONE of: caller-supplied (Type), has-strict-default (most axes), "
    "or has-derived-default (Observability).  Add the missing "
    "specialization, then re-run.");

static_assert(detail::default_coverage::type_defaults_match_substrate(),
    "fixy::Default — a type-valued strict-default aliased here has "
    "drifted from safety::fn::Fn<int>'s shipped default.  Likely "
    "cause: the substrate's per-axis default was changed without "
    "updating fixy/Default.h alongside.  Re-align fixy/Default.h's "
    "specialization with the substrate's authoritative default in "
    "safety/Fn.h's class template parameter list.");

static_assert(detail::default_coverage::enum_defaults_match_substrate(),
    "fixy::Default — an enum-valued strict-default aliased here has "
    "drifted from safety::fn::Fn<int>'s shipped default.  Same fix "
    "as the type-defaults assertion above.");

}  // namespace crucible::fixy
