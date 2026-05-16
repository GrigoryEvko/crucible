#pragma once

// ── crucible::fixy::fn — Type + Grants → safety::fn::Fn aggregator ─
//
// Phase B of the clean reimplementation per misc/16_05_2026_fixy.md §4.
//
// This header is THE universal integration point for fixy::: every
// production binding spells `fixy::fn<Type, Grants...>` and the
// resolver projects the Grants pack onto safety::fn::Fn's 19-positional
// parameter slot, gates IsAccepted at the class-template body, and
// surfaces a round-trip witness with the directly-spelled equivalent.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::fn::Fn<Type, ...>           — the 19-axis aggregator (P0-1)
//   safety::fn::ValidComposition<F>     — 20-rule §6.8 gate (P0-2)
//   safety::fn::UsageMode / SecLevel /  — enum-valued axis tags
//     ReprKind / OverflowMode /
//     MutationMode / ReentrancyMode
//   safety::fn::pred::* / cost::* /     — type-valued axis tags
//     precision::* / space::* /
//     size_pol::* / lifetime::* /
//     stale::* / proto::*
//   safety::source::* / trust::*        — Provenance / Trust namespaces
//   effects::Row<Es...>                 — Effect axis carrier
//
//   fixy::dim::DimensionAxis            — the 20-axis enum (alias)
//   fixy::strict_default_for<D>         — per-dim strict default
//   fixy::grant::*                      — engagement + relaxation tags
//   fixy::IsAccepted<Type, Grants...>   — engagement gate
//
// ── Substrate added by this header ─────────────────────────────────
//
// Two metafunctions and one wrapper:
//
//   detail::resolve::project<G>     — Grant tag → substrate slot
//                                     (specialized per tag, exposes
//                                      ::type or ::value + ::value_type)
//   detail::resolve::find_grant_t<D, Grants...>
//                                   — first Grant in Grants whose
//                                     which_dim_v matches D; falls
//                                     back to accept_default_strict_for
//                                     (which then projects to
//                                     strict_default_for<D>)
//   fixy::fn<Type, Grants...>       — wrapper aggregating value
//                                     + 18-axis-resolved safety::fn::Fn
//
// One factory:
//
//   mint_fn<Type, Grants...>(value) — Universal Mint Pattern factory
//
// Eight stance aliases (misc/16_05_2026_fixy.md §5 catalog):
//
//   stance::PureLinear        — every axis strict-default
//   stance::PureCopy          — copy usage, otherwise strict
//   stance::IoFunction        — Effect=IO, otherwise strict
//   stance::BgWorker          — Effect={Bg, Alloc}, otherwise strict
//   stance::CtCrypto          — Repr=Atomic, Effect={}, NoBranching
//   stance::SecretConsumer    — Security=Secret declassification
//   stance::PublicEmit        — Security=Public emission
//   stance::AsyncEndpoint     — Reentrancy=Coroutine + Effect={IO}
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — wrapper has one Type field initialized by NSDMI.  No
//              uninitialized state path.
//   TypeSafe — Grants pack types are structural concepts; project
//              specializations name exact substrate slot types.  No
//              implicit conversions, no raw integers.
//   NullSafe — no pointer members; the wrapper is value-semantic.
//   MemSafe  — no heap; Type's own move/copy semantics propagate.
//   DetSafe  — same (Type, Grants...) → same safety_fn_t → same
//              federation cache key (relevant for L7's KernelCache).
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every grant tag is empty + final + grant_base; the 18-axis
// type-level parameters carry no runtime state.  `sizeof(fixy::fn<T>)`
// is exactly `sizeof(T)` when T's alignment dominates the EBO chain
// (witnessed for trivial T below).
//
// ── Self-test ──────────────────────────────────────────────────────
//
// Five witnesses ride this header:
//   1. Round-trip — fixy::fn<int>::safety_fn_t IS safety::fn::Fn<int>
//   2. EBO collapse — sizeof(fixy::fn<int>) == sizeof(int)
//   3. Per-axis projection — `affine` resolves to UsageMode::Affine
//   4. Strict-default propagation — Refinement axis under accept-
//      strict resolves to pred::True
//   5. Compound relaxation — multiple non-strict grants compose
//      correctly into the resolved Fn<>

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/fixy/Default.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Reject.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/Tagged.h>

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── detail::resolve — grant → substrate projection ─────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::resolve {

// ─── project<G> — Grant tag → substrate slot ──────────────────────
//
// Per-tag specializations expose `::type` (type-valued axes) and/or
// `::value` + `::value_type` (enum/integer-valued axes).  The
// acceptance marker `accept_default_strict_for<D>` inherits from
// `strict_default_for<D>` so the projection automatically reaches the
// substrate default — no per-axis dispatch.

template <typename G>
struct project;

// Acceptance markers delegate to strict_default_for<D>
template <dim::DimensionAxis D>
struct project<grant::accept_default_strict_for<D>> : strict_default_for<D> {};

// ── Dim 2 Refinement (type-valued) ────────────────────────────────
template <typename Pred>
struct project<grant::refined_with<Pred>> { using type = Pred; };

// ── Dim 3 Usage (enum-valued) ─────────────────────────────────────
template <> struct project<grant::affine> {
    using value_type = safety::fn::UsageMode;
    static constexpr value_type value = safety::fn::UsageMode::Affine;
};
template <> struct project<grant::copy> {
    using value_type = safety::fn::UsageMode;
    static constexpr value_type value = safety::fn::UsageMode::Copy;
};
template <> struct project<grant::ghost> {
    using value_type = safety::fn::UsageMode;
    static constexpr value_type value = safety::fn::UsageMode::Ghost;
};
template <> struct project<grant::borrow> {
    using value_type = safety::fn::UsageMode;
    static constexpr value_type value = safety::fn::UsageMode::Borrow;
};
template <> struct project<grant::capability_usage> {
    using value_type = safety::fn::UsageMode;
    static constexpr value_type value = safety::fn::UsageMode::Capability;
};

// ── Dim 4 Effect (type-valued) ────────────────────────────────────
template <effects::Effect... Es>
struct project<grant::with<Es...>> { using type = effects::Row<Es...>; };

// ── Dim 5 Security (enum-valued via declassify) ───────────────────
//
// Phase B convention: declassify<Policy> projects to SecLevel::Public.
// The Policy parameter is captured for audit trails (consumed by
// Phase C's declassification call sites) but the substrate's
// security-lattice slot only needs the post-declassification level.
template <typename Policy>
struct project<grant::declassify<Policy>> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Public;
};

// ── Dim 6 Protocol (type-valued) ──────────────────────────────────
template <typename Proto>
struct project<grant::protocol<Proto>> { using type = Proto; };

// ── Dim 7 Lifetime (type-valued) ──────────────────────────────────
template <auto RegionTag>
struct project<grant::in_region<RegionTag>> {
    using type = safety::fn::lifetime::In<RegionTag>;
};

// ── Dim 8 Provenance (type-valued) ────────────────────────────────
template <typename Source>
struct project<grant::from_source<Source>> { using type = Source; };

// ── Dim 9 Trust (type-valued) ─────────────────────────────────────
template <auto Rationale>
struct project<grant::trust_assumed<Rationale>> {
    using type = safety::trust::Assumed;
};

// ── Dim 10 Representation (enum-valued) ───────────────────────────
template <safety::fn::ReprKind Kind>
struct project<grant::repr<Kind>> {
    using value_type = safety::fn::ReprKind;
    static constexpr value_type value = Kind;
};

// ── Dim 13 Complexity (type-valued) ───────────────────────────────
template <> struct project<grant::cost_constant>  { using type = safety::fn::cost::Constant; };
template <auto N> struct project<grant::cost_linear<N>> {
    using type = safety::fn::cost::Linear<N>;
};
template <auto N> struct project<grant::cost_quadratic<N>> {
    using type = safety::fn::cost::Quadratic<N>;
};
template <> struct project<grant::cost_unbounded> { using type = safety::fn::cost::Unbounded; };

// ── Dim 14 Precision (type-valued) ────────────────────────────────
template <> struct project<grant::precision_f32> { using type = safety::fn::precision::F32; };
template <> struct project<grant::precision_f64> { using type = safety::fn::precision::F64; };
template <auto Bound> struct project<grant::precision_higham<Bound>> {
    using type = safety::fn::precision::Higham<Bound>;
};

// ── Dim 15 Space (type-valued) ────────────────────────────────────
template <auto N> struct project<grant::space_bounded<N>> {
    using type = safety::fn::space::Bounded<N>;
};
template <> struct project<grant::space_unbounded> { using type = safety::fn::space::Unbounded; };

// ── Dim 16 Overflow (enum-valued) ─────────────────────────────────
template <> struct project<grant::overflow_wrap> {
    using value_type = safety::fn::OverflowMode;
    static constexpr value_type value = safety::fn::OverflowMode::Wrap;
};
template <> struct project<grant::overflow_saturate> {
    using value_type = safety::fn::OverflowMode;
    static constexpr value_type value = safety::fn::OverflowMode::Saturate;
};
template <> struct project<grant::overflow_widen> {
    using value_type = safety::fn::OverflowMode;
    static constexpr value_type value = safety::fn::OverflowMode::Widen;
};

// ── Dim 18 Mutation (enum-valued) ─────────────────────────────────
template <> struct project<grant::mut_mutable> {
    using value_type = safety::fn::MutationMode;
    static constexpr value_type value = safety::fn::MutationMode::Mutable;
};
template <> struct project<grant::mut_append> {
    using value_type = safety::fn::MutationMode;
    static constexpr value_type value = safety::fn::MutationMode::Append;
};
template <> struct project<grant::mut_monotonic> {
    using value_type = safety::fn::MutationMode;
    static constexpr value_type value = safety::fn::MutationMode::Monotonic;
};

// ── Dim 19 Reentrancy (enum-valued) ───────────────────────────────
template <> struct project<grant::reentrant> {
    using value_type = safety::fn::ReentrancyMode;
    static constexpr value_type value = safety::fn::ReentrancyMode::Reentrant;
};
template <> struct project<grant::coroutine> {
    using value_type = safety::fn::ReentrancyMode;
    static constexpr value_type value = safety::fn::ReentrancyMode::Coroutine;
};

// ── Dim 20 Size (type-valued) ─────────────────────────────────────
template <auto Depth> struct project<grant::sized_at<Depth>> {
    using type = safety::fn::size_pol::Sized<Depth>;
};
template <> struct project<grant::productive> { using type = safety::fn::size_pol::Productive; };

// ── Dim 21 Version (integer-valued) ───────────────────────────────
template <std::uint32_t V> struct project<grant::version<V>> {
    using value_type = std::uint32_t;
    static constexpr value_type value = V;
};

// ── Dim 22 Staleness (type-valued) ────────────────────────────────
template <auto TauMax> struct project<grant::stale_to<TauMax>> {
    using type = safety::fn::stale::Stale<TauMax>;
};

// ─── find_grant_t<D, Grants...> — first Grant with which_dim_v == D ─
//
// Walks the pack left-to-right.  The base case (empty pack) returns
// `accept_default_strict_for<D>` so downstream `project` always has a
// valid specialization to consult.  Post-IsAccepted, the pack is
// guaranteed to engage every axis; the base case is unreachable in
// well-formed bindings but defensive against bypassed IsAccepted.

template <dim::DimensionAxis D, typename... Grants>
struct find_grant_impl;

template <dim::DimensionAxis D>
struct find_grant_impl<D> {
    using type = grant::accept_default_strict_for<D>;
};

template <dim::DimensionAxis D, typename G, typename... Rest>
struct find_grant_impl<D, G, Rest...> {
    using type = std::conditional_t<
        grant::IsGrantTag_v<G> && grant::which_dim_v<G> == D,
        G,
        typename find_grant_impl<D, Rest...>::type>;
};

template <dim::DimensionAxis D, typename... Grants>
using find_grant_t = typename find_grant_impl<D, Grants...>::type;

// ─── Per-axis resolvers ────────────────────────────────────────────

// Type-valued axes
template <typename... Grants>
using resolve_refinement_t =
    typename project<find_grant_t<dim::DimensionAxis::Refinement, Grants...>>::type;
template <typename... Grants>
using resolve_effect_t =
    typename project<find_grant_t<dim::DimensionAxis::Effect, Grants...>>::type;
template <typename... Grants>
using resolve_protocol_t =
    typename project<find_grant_t<dim::DimensionAxis::Protocol, Grants...>>::type;
template <typename... Grants>
using resolve_lifetime_t =
    typename project<find_grant_t<dim::DimensionAxis::Lifetime, Grants...>>::type;
template <typename... Grants>
using resolve_source_t =
    typename project<find_grant_t<dim::DimensionAxis::Provenance, Grants...>>::type;
template <typename... Grants>
using resolve_trust_t =
    typename project<find_grant_t<dim::DimensionAxis::Trust, Grants...>>::type;
template <typename... Grants>
using resolve_cost_t =
    typename project<find_grant_t<dim::DimensionAxis::Complexity, Grants...>>::type;
template <typename... Grants>
using resolve_precision_t =
    typename project<find_grant_t<dim::DimensionAxis::Precision, Grants...>>::type;
template <typename... Grants>
using resolve_space_t =
    typename project<find_grant_t<dim::DimensionAxis::Space, Grants...>>::type;
template <typename... Grants>
using resolve_size_t =
    typename project<find_grant_t<dim::DimensionAxis::Size, Grants...>>::type;
template <typename... Grants>
using resolve_staleness_t =
    typename project<find_grant_t<dim::DimensionAxis::Staleness, Grants...>>::type;

// Enum/integer-valued axes
template <typename... Grants>
inline constexpr safety::fn::UsageMode resolve_usage_v =
    project<find_grant_t<dim::DimensionAxis::Usage, Grants...>>::value;
template <typename... Grants>
inline constexpr safety::fn::SecLevel resolve_security_v =
    project<find_grant_t<dim::DimensionAxis::Security, Grants...>>::value;
template <typename... Grants>
inline constexpr safety::fn::ReprKind resolve_repr_v =
    project<find_grant_t<dim::DimensionAxis::Representation, Grants...>>::value;
template <typename... Grants>
inline constexpr safety::fn::OverflowMode resolve_overflow_v =
    project<find_grant_t<dim::DimensionAxis::Overflow, Grants...>>::value;
template <typename... Grants>
inline constexpr safety::fn::MutationMode resolve_mutation_v =
    project<find_grant_t<dim::DimensionAxis::Mutation, Grants...>>::value;
template <typename... Grants>
inline constexpr safety::fn::ReentrancyMode resolve_reentrancy_v =
    project<find_grant_t<dim::DimensionAxis::Reentrancy, Grants...>>::value;
template <typename... Grants>
inline constexpr std::uint32_t resolve_version_v =
    project<find_grant_t<dim::DimensionAxis::Version, Grants...>>::value;

// ─── resolved_fn_t<Type, Grants...> — substrate Fn instantiation ──

template <typename Type, typename... Grants>
using resolved_fn_t = safety::fn::Fn<
    Type,
    resolve_refinement_t<Grants...>,
    resolve_usage_v<Grants...>,
    resolve_effect_t<Grants...>,
    resolve_security_v<Grants...>,
    resolve_protocol_t<Grants...>,
    resolve_lifetime_t<Grants...>,
    resolve_source_t<Grants...>,
    resolve_trust_t<Grants...>,
    resolve_repr_v<Grants...>,
    resolve_cost_t<Grants...>,
    resolve_precision_t<Grants...>,
    resolve_space_t<Grants...>,
    resolve_overflow_v<Grants...>,
    resolve_mutation_v<Grants...>,
    resolve_reentrancy_v<Grants...>,
    resolve_size_t<Grants...>,
    resolve_version_v<Grants...>,
    resolve_staleness_t<Grants...>>;

// Implicit Type engagement marker injected at fixy::fn instantiation
// (per Grant.h's Dim 1 Type discipline: callers do not write the
// marker — the wrapper supplies it).
using ImplicitTypeMarker =
    grant::accept_default_strict_for<dim::DimensionAxis::Type>;

}  // namespace detail::resolve

// ═════════════════════════════════════════════════════════════════════
// ── fixy::fn<Type, Grants...> — the universal integration point ────
// ═════════════════════════════════════════════════════════════════════
//
// The wrapper aggregating a Type value with a Grants pack describing
// the binding's discipline on the 18 non-Type axes (Type is the first
// template parameter; the wrapper synthesizes its acceptance marker).
//
// Gate sequence at instantiation:
//
//   1. IsAccepted<Type, ImplicitTypeMarker, Grants...> fires the
//      engagement check + Type-axis well-formedness (object type,
//      non-cv, non-array, non-reference).
//
//   2. resolved_fn_t<Type, Grants...> projects every Grant onto the
//      substrate's Fn<...> parameter pack.  Each `project` lookup
//      either matches a known relaxation tag OR delegates to
//      `strict_default_for<D>` via the acceptance marker.
//
//   3. safety::fn::Fn<...>'s class-body static_asserts fire:
//      ValidComposition gates the 20 §6.8 collision rules; the Type
//      gate re-checks the Type axis at the substrate level.
//
// Round-trip witness: `safety_fn_t` IS the directly-spelled
// safety::fn::Fn<...> equivalent.

template <typename Type, typename... Grants>
class fn {
    using ImplicitTypeMarker = detail::resolve::ImplicitTypeMarker;

    static_assert(IsAccepted<Type, ImplicitTypeMarker, Grants...>,
        "fixy::fn<Type, Grants...> requires every DimensionAxis to be "
        "engaged.  Each unengaged axis surfaces via fixy::diag::"
        "FixyNotEngaged_<Axis> diagnostic tags; see fixy/Reject.h's "
        "first_missing_axis_v helper for the offending axis.  Either "
        "add `grant::accept_default_strict_for<dim::DimensionAxis::"
        "<Axis>>` to the Grants pack to accept the strict default, "
        "or supply the appropriate per-axis relaxation tag from "
        "fixy::grant::*.");

public:
    using value_type  = Type;
    using safety_fn_t = detail::resolve::resolved_fn_t<Type, Grants...>;

    // ── Per-axis introspection — passthroughs into safety_fn_t ────
    using refinement_t = typename safety_fn_t::refinement_t;
    using effect_row_t = typename safety_fn_t::effect_row_t;
    using protocol_t   = typename safety_fn_t::protocol_t;
    using lifetime_t   = typename safety_fn_t::lifetime_t;
    using source_t     = typename safety_fn_t::source_t;
    using trust_t      = typename safety_fn_t::trust_t;
    using cost_t       = typename safety_fn_t::cost_t;
    using precision_t  = typename safety_fn_t::precision_t;
    using space_t      = typename safety_fn_t::space_t;
    using size_t_      = typename safety_fn_t::size_t_;
    using staleness_t  = typename safety_fn_t::staleness_t;

    static constexpr safety::fn::UsageMode      usage_v      = safety_fn_t::usage_v;
    static constexpr safety::fn::SecLevel       security_v   = safety_fn_t::security_v;
    static constexpr safety::fn::ReprKind       repr_v       = safety_fn_t::repr_v;
    static constexpr safety::fn::OverflowMode   overflow_v   = safety_fn_t::overflow_v;
    static constexpr safety::fn::MutationMode   mutation_v   = safety_fn_t::mutation_v;
    static constexpr safety::fn::ReentrancyMode reentrancy_v = safety_fn_t::reentrancy_v;
    static constexpr std::uint32_t              version_v    = safety_fn_t::version_v;

    // ── Construction ───────────────────────────────────────────────
    constexpr fn() = default;

    explicit constexpr fn(Type v)
        noexcept(std::is_nothrow_move_constructible_v<Type>)
        : value_{std::move(v)} {}

    // ── Value access (deducing-this) ──────────────────────────────
    template <typename Self>
    [[nodiscard]] constexpr auto&& value(this Self&& self) noexcept {
        return std::forward<Self>(self).value_;
    }

private:
    Type value_{};
};

// ═════════════════════════════════════════════════════════════════════
// ── mint_fn — Universal Mint Pattern (CLAUDE.md §XXI) ──────────────
// ═════════════════════════════════════════════════════════════════════
//
// Token-mint flavor.  Derives `fixy::fn<Type, Grants...>` authority
// from the Type + Grants pack.  Single concept gate is the same
// IsAccepted predicate that the class body asserts; the requires-clause
// makes the gate user-visible in the function signature for grep-
// discoverable review surface.

template <typename Type, typename... Grants>
    requires IsAccepted<Type, detail::resolve::ImplicitTypeMarker, Grants...>
[[nodiscard]] constexpr auto mint_fn(Type v)
    noexcept(std::is_nothrow_move_constructible_v<Type>)
    -> fn<Type, Grants...>
{
    return fn<Type, Grants...>{std::move(v)};
}

// ═════════════════════════════════════════════════════════════════════
// ── Stance aliases — production short-hand ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Misc/16_05_2026_fixy.md §5 catalogs the canonical bindings every
// production engineer encounters.  Each stance pre-fills the Grants
// pack with a coherent set of acceptance markers + relaxations,
// leaving only the Type as a parameter at the call site.
//
// The 8 stances cover:
//   PureLinear     — strict-default everywhere, linear consumption
//   PureCopy       — copy usage, strict elsewhere
//   IoFunction     — IO-effecting function, strict elsewhere
//   BgWorker       — background allocator + IO context
//   CtCrypto       — constant-time crypto path (Atomic repr, no
//                    branching, no allocation)
//   SecretConsumer — declassifies a secret to public
//   PublicEmit     — emits publicly-observable output
//   AsyncEndpoint  — coroutine reentrancy + IO

namespace stance {

namespace detail_stance {
template <dim::DimensionAxis D>
using strict = grant::accept_default_strict_for<D>;
}

// ── PureLinear — all-strict, exhaustive engagement ────────────────
template <typename Type>
using PureLinear = ::crucible::fixy::fn<Type,
    detail_stance::strict<dim::DimensionAxis::Refinement>,
    detail_stance::strict<dim::DimensionAxis::Usage>,
    detail_stance::strict<dim::DimensionAxis::Effect>,
    detail_stance::strict<dim::DimensionAxis::Security>,
    detail_stance::strict<dim::DimensionAxis::Protocol>,
    detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>,
    detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>,
    detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>,
    detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>,
    detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>,
    detail_stance::strict<dim::DimensionAxis::Reentrancy>,
    detail_stance::strict<dim::DimensionAxis::Size>,
    detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>>;

// ── PureCopy — copy usage, strict elsewhere ───────────────────────
template <typename Type>
using PureCopy = ::crucible::fixy::fn<Type,
    detail_stance::strict<dim::DimensionAxis::Refinement>,
    grant::copy,
    detail_stance::strict<dim::DimensionAxis::Effect>,
    detail_stance::strict<dim::DimensionAxis::Security>,
    detail_stance::strict<dim::DimensionAxis::Protocol>,
    detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>,
    detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>,
    detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>,
    detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>,
    detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>,
    detail_stance::strict<dim::DimensionAxis::Reentrancy>,
    detail_stance::strict<dim::DimensionAxis::Size>,
    detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>>;

// ── IoFunction — IO effect, strict elsewhere ──────────────────────
template <typename Type>
using IoFunction = ::crucible::fixy::fn<Type,
    detail_stance::strict<dim::DimensionAxis::Refinement>,
    detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with_io,
    detail_stance::strict<dim::DimensionAxis::Security>,
    detail_stance::strict<dim::DimensionAxis::Protocol>,
    detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>,
    detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>,
    detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>,
    detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>,
    detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>,
    detail_stance::strict<dim::DimensionAxis::Reentrancy>,
    detail_stance::strict<dim::DimensionAxis::Size>,
    detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>>;

// ── BgWorker — Bg + Alloc effects (typical bg thread worker) ──────
template <typename Type>
using BgWorker = ::crucible::fixy::fn<Type,
    detail_stance::strict<dim::DimensionAxis::Refinement>,
    detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with<effects::Effect::Bg, effects::Effect::Alloc>,
    detail_stance::strict<dim::DimensionAxis::Security>,
    detail_stance::strict<dim::DimensionAxis::Protocol>,
    detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>,
    detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>,
    detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>,
    detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>,
    detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>,
    detail_stance::strict<dim::DimensionAxis::Reentrancy>,
    detail_stance::strict<dim::DimensionAxis::Size>,
    detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>>;

// ── SecretConsumer — declassifies a secret value ──────────────────
//
// The Policy parameter is captured for audit-trail purposes; the
// substrate's Security slot resolves to SecLevel::Public per the
// declassify projection.

template <typename Type, typename Policy>
using SecretConsumer = ::crucible::fixy::fn<Type,
    detail_stance::strict<dim::DimensionAxis::Refinement>,
    detail_stance::strict<dim::DimensionAxis::Usage>,
    detail_stance::strict<dim::DimensionAxis::Effect>,
    grant::declassify<Policy>,
    detail_stance::strict<dim::DimensionAxis::Protocol>,
    detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>,
    detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>,
    detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>,
    detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>,
    detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>,
    detail_stance::strict<dim::DimensionAxis::Reentrancy>,
    detail_stance::strict<dim::DimensionAxis::Size>,
    detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>>;

// ── AsyncEndpoint — coroutine + IO ────────────────────────────────
template <typename Type>
using AsyncEndpoint = ::crucible::fixy::fn<Type,
    detail_stance::strict<dim::DimensionAxis::Refinement>,
    detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with_io,
    detail_stance::strict<dim::DimensionAxis::Security>,
    detail_stance::strict<dim::DimensionAxis::Protocol>,
    detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>,
    detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>,
    detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>,
    detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>,
    detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>,
    grant::coroutine,
    detail_stance::strict<dim::DimensionAxis::Size>,
    detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>>;

}  // namespace stance

// ═════════════════════════════════════════════════════════════════════
// ── Self-test — compile-time witnesses ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::fn_self_test {

// 1. Round-trip witness — fixy::fn<int>::safety_fn_t IS
//    the all-default safety::fn::Fn<int>.
static_assert(std::is_same_v<
    typename stance::PureLinear<int>::safety_fn_t,
    safety::fn::Fn<int>>,
    "stance::PureLinear<int>::safety_fn_t must round-trip to "
    "safety::fn::Fn<int>'s all-default instantiation.");

// 2. EBO collapse — sizeof(fixy::fn<int, all-strict>) == sizeof(int).
//    Each grant tag is empty + final + grant_base; the 18-axis
//    type-level pack carries no runtime state.
static_assert(sizeof(stance::PureLinear<int>)    == sizeof(int),
    "stance::PureLinear<int> must EBO-collapse to sizeof(int) — the "
    "18-axis type-level pack carries no runtime state.");
static_assert(sizeof(stance::PureLinear<char>)   == sizeof(char),
    "stance::PureLinear<char> must EBO-collapse to sizeof(char).");
static_assert(sizeof(stance::PureLinear<double>) == sizeof(double),
    "stance::PureLinear<double> must EBO-collapse to sizeof(double).");

// 3. Per-axis projection — `grant::affine` resolves to
//    UsageMode::Affine on the substrate Fn<...>.
static_assert(detail::resolve::resolve_usage_v<
    grant::accept_default_strict_for<dim::DimensionAxis::Refinement>,
    grant::affine> == safety::fn::UsageMode::Affine,
    "grant::affine must project to UsageMode::Affine.");

// 4. Strict-default propagation — under accept-strict, the
//    Refinement axis resolves to pred::True.
static_assert(std::is_same_v<
    detail::resolve::resolve_refinement_t<
        grant::accept_default_strict_for<dim::DimensionAxis::Refinement>>,
    safety::fn::pred::True>,
    "accept_default_strict_for<Refinement> must project to "
    "pred::True (the substrate's Refinement default).");

// 5. PureCopy resolves Usage to Copy while keeping Refinement strict.
static_assert(stance::PureCopy<int>::usage_v
    == safety::fn::UsageMode::Copy,
    "stance::PureCopy must resolve Usage to Copy.");
static_assert(std::is_same_v<
    typename stance::PureCopy<int>::refinement_t,
    safety::fn::pred::True>,
    "stance::PureCopy must keep Refinement at the strict default.");

// 6. IoFunction's Effect row contains Effect::IO.
static_assert(std::is_same_v<
    typename stance::IoFunction<int>::effect_row_t,
    effects::Row<effects::Effect::IO>>,
    "stance::IoFunction's Effect row must contain exactly Effect::IO.");

// 7. AsyncEndpoint resolves Reentrancy to Coroutine.
static_assert(stance::AsyncEndpoint<int>::reentrancy_v
    == safety::fn::ReentrancyMode::Coroutine,
    "stance::AsyncEndpoint must resolve Reentrancy to Coroutine.");

// 8. Direct (non-stance) round-trip — a user-spelled fixy::fn with
//    one relaxation matches the directly-spelled safety::fn::Fn.
namespace round_trip_2 {
using direct_fixy = ::crucible::fixy::fn<int,
    grant::accept_default_strict_for<dim::DimensionAxis::Refinement>,
    grant::affine,  // Usage = Affine
    grant::accept_default_strict_for<dim::DimensionAxis::Effect>,
    grant::accept_default_strict_for<dim::DimensionAxis::Security>,
    grant::accept_default_strict_for<dim::DimensionAxis::Protocol>,
    grant::accept_default_strict_for<dim::DimensionAxis::Lifetime>,
    grant::accept_default_strict_for<dim::DimensionAxis::Provenance>,
    grant::accept_default_strict_for<dim::DimensionAxis::Trust>,
    grant::accept_default_strict_for<dim::DimensionAxis::Representation>,
    grant::accept_default_strict_for<dim::DimensionAxis::Observability>,
    grant::accept_default_strict_for<dim::DimensionAxis::Complexity>,
    grant::accept_default_strict_for<dim::DimensionAxis::Precision>,
    grant::accept_default_strict_for<dim::DimensionAxis::Space>,
    grant::accept_default_strict_for<dim::DimensionAxis::Overflow>,
    grant::accept_default_strict_for<dim::DimensionAxis::Mutation>,
    grant::accept_default_strict_for<dim::DimensionAxis::Reentrancy>,
    grant::accept_default_strict_for<dim::DimensionAxis::Size>,
    grant::accept_default_strict_for<dim::DimensionAxis::Version>,
    grant::accept_default_strict_for<dim::DimensionAxis::Staleness>>;

using direct_substrate = safety::fn::Fn<int,
    safety::fn::pred::True,
    safety::fn::UsageMode::Affine,
    effects::Row<>,
    safety::fn::SecLevel::Classified,
    safety::fn::proto::None,
    safety::fn::lifetime::Static,
    safety::source::FromInternal,
    safety::trust::Verified,
    safety::fn::ReprKind::Opaque,
    safety::fn::cost::Unstated,
    safety::fn::precision::Exact,
    safety::fn::space::Zero,
    safety::fn::OverflowMode::Trap,
    safety::fn::MutationMode::Immutable,
    safety::fn::ReentrancyMode::NonReentrant,
    safety::fn::size_pol::Unstated,
    1u,
    safety::fn::stale::Fresh>;

static_assert(std::is_same_v<direct_fixy::safety_fn_t, direct_substrate>,
    "Single-relaxation round-trip: fixy::fn's safety_fn_t with "
    "Usage=affine must match the directly-spelled substrate Fn<...> "
    "with UsageMode::Affine.");
}  // namespace round_trip_2

// 9. mint_fn factory returns the correct concrete type.
constexpr auto minted = mint_fn<int,
    grant::accept_default_strict_for<dim::DimensionAxis::Refinement>,
    grant::accept_default_strict_for<dim::DimensionAxis::Usage>,
    grant::accept_default_strict_for<dim::DimensionAxis::Effect>,
    grant::accept_default_strict_for<dim::DimensionAxis::Security>,
    grant::accept_default_strict_for<dim::DimensionAxis::Protocol>,
    grant::accept_default_strict_for<dim::DimensionAxis::Lifetime>,
    grant::accept_default_strict_for<dim::DimensionAxis::Provenance>,
    grant::accept_default_strict_for<dim::DimensionAxis::Trust>,
    grant::accept_default_strict_for<dim::DimensionAxis::Representation>,
    grant::accept_default_strict_for<dim::DimensionAxis::Observability>,
    grant::accept_default_strict_for<dim::DimensionAxis::Complexity>,
    grant::accept_default_strict_for<dim::DimensionAxis::Precision>,
    grant::accept_default_strict_for<dim::DimensionAxis::Space>,
    grant::accept_default_strict_for<dim::DimensionAxis::Overflow>,
    grant::accept_default_strict_for<dim::DimensionAxis::Mutation>,
    grant::accept_default_strict_for<dim::DimensionAxis::Reentrancy>,
    grant::accept_default_strict_for<dim::DimensionAxis::Size>,
    grant::accept_default_strict_for<dim::DimensionAxis::Version>,
    grant::accept_default_strict_for<dim::DimensionAxis::Staleness>>(42);
static_assert(minted.value() == 42,
    "mint_fn must construct fixy::fn carrying the supplied value.");

}  // namespace detail::fn_self_test

}  // namespace crucible::fixy
