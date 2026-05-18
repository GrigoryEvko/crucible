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
//   stance::CtCrypto          — constant-time path: as_secret + with<>
//                                (no IO; linear consume of Secret)
//   stance::SecretConsumer    — Security=Public via declassify<Policy>
//   stance::PublicEmit        — IO + declassify<Policy> audit trail
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
#include <crucible/fixy/Profile.h>
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

// FIXY-LAT-Security: explicit Security lattice point projections —
// every SecLevel enumerator reachable through a named grant tag.
template <> struct project<grant::as_unclassified> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Unclassified;
};
template <> struct project<grant::as_public> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Public;
};
template <> struct project<grant::as_internal> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Internal;
};
template <> struct project<grant::as_classified> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Classified;
};
template <> struct project<grant::as_secret> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Secret;
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

// FIXY-LAT-Trust: explicit Trust lattice point projections — every
// safety::trust::* tag reachable through a named grant tag.
template <> struct project<grant::trust_verified>   { using type = safety::trust::Verified; };
template <> struct project<grant::trust_tested>     { using type = safety::trust::Tested; };
template <> struct project<grant::trust_unverified> { using type = safety::trust::Unverified; };
template <> struct project<grant::trust_external>   { using type = safety::trust::External; };

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

// FIXY-AUDIT-A1: gate the `which_dim_v<G>` lookup on `IsGrantTag_v<G>`
// to avoid eager substitution.  The previous std::conditional_t form
// instantiates BOTH branches; `which_dim_v<G>` for a non-grant G
// (e.g. someone accidentally seeds the pack with a raw type) is a
// hard error inside the resolver rather than a clean rejection at
// IsAccepted.
//
// The fix splits into two partial specializations:
//   (1) primary unconstrained-on-G recursion — skip G as non-matching.
//   (2) constrained specialization for an actual grant tag whose
//       which_dim_v equals D — returns G.
// Constraint partial-ordering picks the more-constrained (2) when G
// is a real grant on axis D; else (1) recurses without ever touching
// `which_dim_v<G>`.
template <dim::DimensionAxis D, typename G, typename... Rest>
struct find_grant_impl<D, G, Rest...> {
    using type = typename find_grant_impl<D, Rest...>::type;
};

template <dim::DimensionAxis D, typename G, typename... Rest>
    requires (grant::IsGrantTag_v<G> && grant::which_dim_v<G> == D)
struct find_grant_impl<D, G, Rest...> {
    using type = G;
};

template <dim::DimensionAxis D, typename... Grants>
using find_grant_t = typename find_grant_impl<D, Grants...>::type;

// ─── declassify<Policy> Policy projection ─────────────────────────
//
// FIXY-AUDIT-A2: the Security axis's `declassify<Policy>` grant
// captures a Policy tag for audit trails, but the substrate's
// security-lattice slot only sees the post-declassification SecLevel
// (`Public`).  Without explicit projection the Policy tag is invisible
// to downstream consumers — they cannot identify which named
// declassification policy authorized a fixy::fn binding.
//
// `find_declassify_policy_t<Grants...>` linearly scans Grants for a
// `grant::declassify<P>` shape and returns `P`; if no declassify
// grant appears (Security defaulted, or pinned via `as_*` tag), it
// returns `void`.  Pattern-match — no `IsGrantTag_v` gate needed since
// the only matching shape is `grant::declassify<P>` itself.
template <typename... Grants> struct find_declassify_policy {
    using type = void;
};
template <typename Policy, typename... Rest>
struct find_declassify_policy<grant::declassify<Policy>, Rest...> {
    using type = Policy;
};
template <typename G, typename... Rest>
struct find_declassify_policy<G, Rest...>
    : find_declassify_policy<Rest...> {};

template <typename... Grants>
using find_declassify_policy_t =
    typename find_declassify_policy<Grants...>::type;

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

// ─── IsAcceptedFn — REMOVED, fixy-H-05 ──────────────────────────────
//
// FIXY-AUDIT-A8 originally factored an `IsAcceptedFn<Type, Grants...>`
// alias that injected `detail::resolve::ImplicitTypeMarker` into the
// substrate's low-level `IsAccepted` concept.  fixy-H-05 inverted the
// public-name discipline: the SIMPLE name `fixy::IsAccepted<Type,
// Grants...>` (Reject.h §IsAccepted, wrapper-discipline gate) now
// auto-injects the marker via its own private `ImplicitTypeMarker`
// alias.  The QUALIFIED name `fixy::IsAcceptedDirect<Type, Grants...>`
// remains the low-level form that takes ALL engagement markers
// explicitly.
//
// Consequence: `IsAcceptedFn` is now a trivial restatement of
// `IsAccepted` and is removed.  Every consumer (fn<>'s class-body
// requires-clause, `mint_fn`'s requires-clause, `mint_fn_for`) routes
// through `fixy::IsAccepted` directly.

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

    // fixy-H-03: surface the per-axis FixyNotEngaged_<Axis>,
    // FixyDuplicate_<Axis>, and FixyMalformedGrant diagnostic tag
    // CLASS NAMES in the compiler's instantiation chain.  Placed at
    // the TOP of the class body — before the H-02 tier static_assert
    // chain — so the Diagnose<Tag> base-class instantiation fires
    // its own static_assert FIRST, surfacing the offending Fixy*
    // tag's class name in the compiler's "required from" trail
    // BEFORE the H-02 tier static_assert (which would otherwise
    // halt class-body processing and suppress the Diagnose firing).
    //
    // Per-tier guards in malformed_grant_or_void_t /
    // missing_tag_or_void_t / duplicate_tag_or_void_t ensure each
    // Diagnose fires only when its tier is the FIRST failing tier
    // (avoids cascade — a malformed-grant doesn't ALSO surface
    // missing-axis since AllDimsEngaged is meaningless then).
    //
    // OK case: each *_or_void_t resolves to `void`, matches the
    // empty DiagnoseAxis*/DiagnoseMalformedGrant<void> spec, no
    // diagnostic fires.  Failure case: resolves to the real Fixy*
    // tag, instantiates the primary template, fires its inner
    // static_assert AND surfaces the tag in the instantiation chain.

    using fixy_h03_tier2_diag_tag =
        malformed_grant_or_void_t<ImplicitTypeMarker, Grants...>;
    using fixy_h03_tier3_diag_tag =
        missing_tag_or_void_t<ImplicitTypeMarker, Grants...>;
    using fixy_h03_tier4_diag_tag =
        duplicate_tag_or_void_t<ImplicitTypeMarker, Grants...>;

    struct fixy_h03_tier2_diagnose
        : DiagnoseMalformedGrant<fixy_h03_tier2_diag_tag> {};
    struct fixy_h03_tier3_diagnose
        : DiagnoseAxisNotEngaged<fixy_h03_tier3_diag_tag> {};
    struct fixy_h03_tier4_diagnose
        : DiagnoseAxisDuplicate<fixy_h03_tier4_diag_tag> {};

    // [temp.inst]/9: member classes of a class template are NOT
    // implicitly instantiated even if their enclosing template is.
    // Force instantiation via sizeof so the Diagnose<Tag> primary
    // template's inner static_assert fires (and surfaces the tag
    // class name in the compiler's "required from" trail) BEFORE
    // the H-02 tier static_assert halts class-body processing.

    static_assert(sizeof(fixy_h03_tier2_diagnose) >= 1,
        "fixy-H-03: force tier-2 DiagnoseMalformedGrant<Tag> instantiation");
    static_assert(sizeof(fixy_h03_tier3_diagnose) >= 1,
        "fixy-H-03: force tier-3 DiagnoseAxisNotEngaged<Tag> instantiation");
    static_assert(sizeof(fixy_h03_tier4_diagnose) >= 1,
        "fixy-H-03: force tier-4 DiagnoseAxisDuplicate<Tag> instantiation");

    // fixy-H-02: branched static_assert chain.  Each tier guards the
    // next via `!prior_failed || this_check`, so only the FIRST failing
    // tier surfaces its diagnostic message.  Replaces the prior single
    // static_assert that always said "axis not engaged" even when the
    // real failure was AllGrantsWellFormed, UniqueEngagementPerAxis,
    // type_is_accepted_payload, or NotInTheoryCorpus.  Each tier names
    // the specific inspection helper a downstream author should consult
    // to identify the offending entry/axis.

    static constexpr bool fixy_h02_tier1_type_ok =
        detail::accept::type_is_accepted_payload<Type>();
    static_assert(fixy_h02_tier1_type_ok,
        "fixy::fn<Type, Grants...> [tier 1: IsAccepted gate]: Type must be "
        "a non-cv, non-array, non-reference, non-function, non-void "
        "object type. "
        "Cite: fixy::detail::accept::type_is_accepted_payload.  "
        "Wrap bare function types as pointers or callables "
        "(std::function_ref) before instantiating fixy::fn.");

    static constexpr bool fixy_h02_tier2_grants_well_formed =
        !fixy_h02_tier1_type_ok
        || AllGrantsWellFormed<ImplicitTypeMarker, Grants...>;
    static_assert(fixy_h02_tier2_grants_well_formed,
        "fixy::fn<Type, Grants...> [tier 2: IsAccepted gate / "
        "AllGrantsWellFormed]: Grants pack contains a malformed "
        "grant (a type that does NOT satisfy fixy::grant::IsGrantTag — "
        "the entry is not final-class, does not inherit "
        "fixy::grant::grant_base, or is a non-grant type entirely such "
        "as `int` or a user struct).  See fixy::diag::FixyMalformedGrant "
        "for the structured diagnostic tag.  Common cause: copy-paste "
        "typo, misspelled grant name, or substrate type accidentally "
        "passed where a grant tag was expected.");

    // Sketch mode (CRUCIBLE_FIXY_STRICT=0) relaxes the engagement
    // axis per Profile.h's contract: "sketch mode permissivity applies
    // only to the engagement axis + theory-corpus checks, never to
    // the §6.8 collision rules."  Tier 3 is the engagement check;
    // appending `|| !fixy_is_strict` short-circuits the assert under
    // sketch.  Tiers 1 (Type validity), 2 (grant well-formedness),
    // and 4 (unique engagement / collision rule) stay strict in both
    // modes — sketch mode does NOT bypass correctness, only relaxes
    // the "every axis must be engaged" rule for in-progress migrations.
    static constexpr bool fixy_h02_tier3_all_dims_engaged =
        !fixy_h02_tier1_type_ok
        || !fixy_h02_tier2_grants_well_formed
        || AllDimsEngaged<ImplicitTypeMarker, Grants...>
        || !fixy_is_strict;
    // fixy-H-15: route through P2741R3 dynamic message so the resolved
    // FixyNotEngaged_<Axis> tag NAME appears literally in the diagnostic
    // text (e.g. "Missing-axis diagnostic tag: FixyNotEngaged_Effect").
    // `tier3_missing_tag_message_v` wraps `first_missing_tag_t<Grants...>`
    // (the helper documented in Reject.h §"Failure inspection" but
    // previously dead architectural plumbing) into a `std::string_view`
    // promoted to static storage via `std::define_static_string`.  When
    // tier 3 succeeds the variable evaluates to an empty string_view
    // and the message is unused; the helper's `if constexpr
    // (AllDimsEngaged<...>)` guard sidesteps the
    // `requires (!AllDimsEngaged<...>)` clause on `first_missing_tag_t`.
    static_assert(fixy_h02_tier3_all_dims_engaged,
        tier3_missing_tag_message_v<ImplicitTypeMarker, Grants...>);

    static constexpr bool fixy_h02_tier4_unique_engagement =
        !fixy_h02_tier1_type_ok
        || !fixy_h02_tier2_grants_well_formed
        || !fixy_h02_tier3_all_dims_engaged
        || UniqueEngagementPerAxis<ImplicitTypeMarker, Grants...>;
    static_assert(fixy_h02_tier4_unique_engagement,
        "fixy::fn<Type, Grants...> [tier 4: IsAccepted gate / "
        "UniqueEngagementPerAxis]: at least one DimensionAxis is "
        "engaged MORE THAN ONCE by the Grants pack (FIXY-AUDIT-A3). "
        "See fixy::first_duplicate_axis_v<Grants...> for the specific "
        "axis + fixy::first_duplicate_tag_t<Grants...> for the "
        "FixyDuplicate_<Axis> diagnostic tag.  Remove the redundant "
        "grant(s).  Note: explicitly writing "
        "`grant::accept_default_strict_for<dim::DimensionAxis::Type>` "
        "is FORBIDDEN (FIXY-AUDIT-A7) — fixy::fn implicitly engages "
        "Type, so the explicit Type marker would trigger a duplicate "
        "on the Type axis.");

    // Tier 5 is the §30.14 corpus check.  Profile.h documents the
    // sketch-mode relaxation as "engagement axis + theory-corpus
    // checks" — both relax under !fixy_is_strict.  Tiers 1/2/4 stay
    // strict in both modes because they enforce the §6.8 collision
    // rules (a non-negotiable correctness floor) and basic input
    // shape (Type validity, grant well-formedness).
    static constexpr bool fixy_h02_tier5_not_in_corpus =
        !fixy_h02_tier1_type_ok
        || !fixy_h02_tier2_grants_well_formed
        || !fixy_h02_tier3_all_dims_engaged
        || !fixy_h02_tier4_unique_engagement
        || theory::NotInTheoryCorpus<Type, ImplicitTypeMarker, Grants...>
        || !fixy_is_strict;
    // fixy-H-13 + fixy-H-16: surface BOTH the matched corpus entry's
    // struct name AND its `cite()` text in the rejection diagnostic
    // via P2741R3 (user-generated static_assert messages).
    // `corpus_full_diagnostic_v` concatenates "matched corpus entry:
    // <name> — <cite>" into static storage via P3491R3
    // `std::define_static_string`.  H-13 gave us the cite — paper,
    // year, pattern explanation, and per-entry remediation — so the
    // diagnostic identifies the literature reference.  H-16 adds the
    // entry's struct name — so a maintainer who sees the diagnostic
    // can grep Theory.h for the matched entry directly.  Together the
    // doc-block claim at Theory.h §IsAccepted-composition ("names
    // which corpus entry matched (paper + year)") is supported by
    // code: name() supplies the entry identifier, cite() supplies the
    // paper + year + remediation prose.  When tier 5 succeeds (no
    // corpus match), corpus_full_diagnostic_v returns an empty
    // string_view; the static_assert message is unused in that case.
    static_assert(fixy_h02_tier5_not_in_corpus,
        theory::corpus_full_diagnostic_v<Type, ImplicitTypeMarker, Grants...>);

public:
    using value_type  = Type;
    using safety_fn_t = detail::resolve::resolved_fn_t<Type, Grants...>;

    // ── Declassify policy accessor (FIXY-AUDIT-A2) ────────────────
    // Resolves to the `Policy` parameter of any `grant::declassify<P>`
    // grant in the pack, else `void`.  Downstream audit code identifies
    // which named declassification policy authorized this binding via
    // `fn<...>::policy_t`.
    using policy_t = detail::resolve::find_declassify_policy_t<Grants...>;

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

    // ── Construction + copy/move discipline (FIXY-AUDIT-A6) ───────
    //
    // POLICY: fixy::fn does NOT override Type's copy/move semantics.
    // Even when `usage_v == UsageMode::Linear`, the wrapper inherits
    // Type's default copy/move/dtor.  Rationale:
    //
    //   • The Linear grade is INFORMATION about how the binding is
    //     intended to be consumed downstream (Permission discipline,
    //     session protocols, ownership audit) — not a runtime lifetime
    //     constraint on the value's storage.  `fixy::fn<int, Linear-
    //     grants>` must remain copyable because `int` is.
    //
    //   • Discipline enforcement is the JOB of `safety::Linear<T>`,
    //     which IS move-only via deleted copy.  Production code that
    //     wants the runtime guarantee declares `fixy::fn<safety::
    //     Linear<T>, Linear-grants>` — the Type is itself move-only,
    //     and fixy::fn's defaulted copy/move correctly disappears.
    //
    //   • Pinning fixy::fn's copy/move to the Linear grade would
    //     conflate two orthogonal concerns: the wrapper's structural
    //     copy semantics (driven by Type) and the binding's lifecycle
    //     contract (driven by the grade).  fixy::fn is a documentation
    //     + integration layer; lifecycle wrappers compose INTO it.
    //
    // Cost-of-violation: none from this policy; the grade is auditable
    // via `usage_v` and `safety_fn_t::usage_v` at every call site, and
    // downstream code that consumes a Linear-grade binding can require
    // `safety::IsLinear<Type>` to refuse non-linear payloads.
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
    requires IsAcceptedActive<Type, Grants...>
[[nodiscard]] constexpr auto mint_fn(Type v)
    noexcept(std::is_nothrow_move_constructible_v<Type>)
    -> fn<Type, Grants...>
{
    return fn<Type, Grants...>{std::move(v)};
}

// ─── mint_fn_for<Stance>(value) — stance-bound mint convenience ────
//
// FIXY-AUDIT-A11 + fixy-H-01: Universal-Mint-Pattern entry point for
// stance::* aliases.  `mint_fn_for<stance::PureCopy>(42)` deduces Type
// from the argument and instantiates the stance with that Type.
//
// Two overloads, separated by stance arity:
//
//   • UNARY  — `template<typename> class Stance` — covers PureLinear /
//     PureCopy / IoFunction / BgWorker / CtCrypto / AsyncEndpoint.
//     Call form: `mint_fn_for<stance::PureCopy>(value)`.
//
//   • BINARY — `template<typename, typename> class Stance` — covers
//     `stance::SecretConsumer<Type, Policy>` and
//     `stance::PublicEmit<Type, Policy>` whose declassify-policy tag is
//     captured as the second stance parameter.  Policy is non-deducible
//     so it appears second in the function template list (after Stance,
//     before Type) to let Type still deduce from the runtime argument.
//     Call form: `mint_fn_for<stance::SecretConsumer, MyPolicy>(value)`.
//
// Per CLAUDE.md §XXI, every mint factory MUST attach a single concept
// gate.  fixy-H-01 hardened both overloads with `StanceForUnary` /
// `StanceForBinary` so Type-axis violations (void / array / reference /
// cv-qualified / function-typed Type) are rejected BEFORE Stance<Type>
// would instantiate; this names the failure at the function signature
// rather than parser-level deduction failure or function-parameter
// declaration ill-formedness.  Engagement-level violations still
// surface via fn<>'s class-body static_assert chain.
//
// Token-mint flavor (no Ctx).  Cost-of-violation: a stance that fails
// IsAccepted for the deduced Type fires the same FixyNotEngaged_*
// diagnostic chain as a direct mint_fn call.
//
// ── StanceFor* concept gates ─────────────────────────────────────
namespace detail {

template <typename T>
concept TypeIsStanceCompatible =
       !std::is_void_v<T>
    && !std::is_array_v<T>
    && !std::is_reference_v<T>
    && !std::is_const_v<T>
    && !std::is_volatile_v<T>
    && !std::is_function_v<T>;

}  // namespace detail

template <template<typename> class Stance, typename Type>
concept StanceForUnary = detail::TypeIsStanceCompatible<Type>;

template <template<typename, typename> class Stance,
          typename Type, typename Policy>
concept StanceForBinary = detail::TypeIsStanceCompatible<Type>;

// ── mint_fn_for — unary stance overload (Type deduced from arg) ──
template <template<typename> class Stance, typename Type>
    requires StanceForUnary<Stance, Type>
[[nodiscard]] constexpr auto mint_fn_for(Type v)
    noexcept(std::is_nothrow_move_constructible_v<Type>)
    -> Stance<Type>
{
    return Stance<Type>{std::move(v)};
}

// ── mint_fn_for — binary stance overload (Policy explicit, Type deduced) ──
//
// Policy precedes Type in the template-arg list so the call site
// `mint_fn_for<stance::SecretConsumer, MyPolicy>(42)` lets the compiler
// deduce Type from the runtime argument while Policy stays explicit
// (it is a phantom tag with no runtime carrier).
template <template<typename, typename> class Stance,
          typename Policy, typename Type>
    requires StanceForBinary<Stance, Type, Policy>
[[nodiscard]] constexpr auto mint_fn_for(Type v)
    noexcept(std::is_nothrow_move_constructible_v<Type>)
    -> Stance<Type, Policy>
{
    return Stance<Type, Policy>{std::move(v)};
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
//   CtCrypto       — constant-time crypto path: as_secret + with<>
//                    (consumes Secret linearly, NO IO)
//   SecretConsumer — declassifies a secret to public via declassify
//   PublicEmit     — IO + declassify<Policy> audit-trail emission
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
    detail_stance::strict<dim::DimensionAxis::Staleness>,
    detail_stance::strict<dim::DimensionAxis::Synchronization>>;

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
    detail_stance::strict<dim::DimensionAxis::Staleness>,
    detail_stance::strict<dim::DimensionAxis::Synchronization>>;

// ── IoFunction — IO effect, public-emit Security, strict elsewhere ─
//
// IoFunction emits data via I/O.  Per Theory.h §30.14
// (classified_io_without_declassify), a binding that engages IO MUST
// either declassify or pin Security to a non-classified level — the
// I/O channel is observable and would otherwise leak a classified
// value.  IoFunction pins `as_public` (= SecLevel::Public) at the
// stance level: callers whose payload is publicly-observable get
// IoFunction; callers whose payload is classified-but-audit-trail-
// authorized for IO use `PublicEmit<T, Policy>` (declassify form).
//
// Pre-fixy-CR-01 IoFunction shipped `strict<Security>` (= Classified),
// which silently bypassed the corpus via the strict-default
// projection.  The fix pins `as_public` explicitly so the stance
// matches its documented semantics.
template <typename Type>
using IoFunction = ::crucible::fixy::fn<Type,
    detail_stance::strict<dim::DimensionAxis::Refinement>,
    detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with_io,
    grant::as_public,
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
    detail_stance::strict<dim::DimensionAxis::Staleness>,
    detail_stance::strict<dim::DimensionAxis::Synchronization>>;

// ── BgWorker — Bg + Alloc effects, public Security, strict else ───
//
// BgWorker spawns work into a background-thread context.  Per
// Theory.h §30.14 (classified_bg_without_declassify), the spawn is
// itself a scheduler-observable event — a classified-value-dependent
// spawn leaks the value through interleaving timing.  BgWorker pins
// `as_public` at the stance level: bg workers carry routing /
// scheduling metadata (non-sensitive by design); workers that
// process classified payloads use `SecretConsumer<T, Policy>` or
// compose `declassify<Policy> + with<Bg, Alloc>` explicitly.
//
// Pre-fixy-CR-01 BgWorker shipped `strict<Security>`, silently
// bypassing the corpus.  The fix pins `as_public` explicitly.
template <typename Type>
using BgWorker = ::crucible::fixy::fn<Type,
    detail_stance::strict<dim::DimensionAxis::Refinement>,
    detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with<effects::Effect::Bg, effects::Effect::Alloc>,
    grant::as_public,
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
    detail_stance::strict<dim::DimensionAxis::Staleness>,
    detail_stance::strict<dim::DimensionAxis::Synchronization>>;

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
    detail_stance::strict<dim::DimensionAxis::Staleness>,
    detail_stance::strict<dim::DimensionAxis::Synchronization>>;

// ── CtCrypto — constant-time crypto path (FIXY-AUDIT-B3) ──────────
//
// Constant-time discipline: handles Secret data, performs NO IO (any
// IO trip would create a timing-observable side channel), and consumes
// its input linearly (Usage=Linear = the strict default — duplicating
// a secret defeats the discipline).  Effect row is explicitly empty
// via `with<>` to pin "no Bg, no Alloc, no IO, no Block" at the type
// level; Security is pinned to `Secret` via `as_secret`.  The §30.14
// classified-IO-without-declassify detector does NOT fire — `has_io`
// is false, so the implicit-flow rule is structurally satisfied.
//
// Rationale for axis choices:
//   - Security = as_secret   pin Secret; the value MUST NOT escape
//     declassified.
//   - Effect   = with<>      no Bg/IO/Alloc/Block — pure compute path.
//     The strict default for Effect IS Row<>; we engage explicitly
//     via `with<>` to make the constant-time discipline self-
//     documenting at the signature level rather than relying on the
//     implicit accept-default marker.
//   - Usage    = strict (=Linear)   linear consumption.  The strict
//     default is Linear per safety/Fn.h::usage_v; the engagement
//     marker pins it explicitly.
//   - Reentrancy = strict (=NonReentrant)   constant-time paths must
//     not interleave with themselves; the strict default is
//     NonReentrant.

template <typename Type>
using CtCrypto = ::crucible::fixy::fn<Type,
    detail_stance::strict<dim::DimensionAxis::Refinement>,
    detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with<>,
    grant::as_secret,
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
    detail_stance::strict<dim::DimensionAxis::Staleness>,
    detail_stance::strict<dim::DimensionAxis::Synchronization>>;

// ── PublicEmit<Policy> — publicly-observable emission (FIXY-AUDIT-B3) ─
//
// Public-emission discipline: emits data via IO with an audit-trail-
// discharging `declassify<Policy>` grant.  The Policy parameter is
// captured for downstream identification of which named declassification
// authorized the public emission (greppable via
// `fn<...>::policy_t`).  The substrate's Security slot resolves to
// SecLevel::Public per the declassify projection.
//
// Why declassify<Policy> rather than as_public:
//   - `as_public` pins SecLevel::Public but carries NO audit trail.
//     A grep over `as_public` reveals every public-emission call site
//     but yields no Policy provenance.
//   - `declassify<Policy>` carries the Policy tag through the type,
//     surfaces via `fn<...>::policy_t`, and pins SecLevel::Public.
//     The audit trail is recoverable at the type level.
//
// The §30.14 classified-IO-without-declassify detector does NOT fire
// — `has_secret=false` (declassify is not in is_secret_grant), so
// the implicit-flow rule is structurally satisfied regardless of the
// IO grant.

template <typename Type, typename Policy>
using PublicEmit = ::crucible::fixy::fn<Type,
    detail_stance::strict<dim::DimensionAxis::Refinement>,
    detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with_io,
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
    detail_stance::strict<dim::DimensionAxis::Staleness>,
    detail_stance::strict<dim::DimensionAxis::Synchronization>>;

// ── AsyncEndpoint — coroutine + IO + public Security ──────────────
//
// AsyncEndpoint is an IO-effecting coroutine.  Same reasoning as
// IoFunction (Theory.h §30.14): the IO emission requires Security
// to be public-or-declassified.  Pins `as_public` at the stance
// level; secret-carrying async endpoints compose `declassify<P>` +
// `with_io` + `coroutine` explicitly.
//
// Pre-fixy-CR-01 AsyncEndpoint shipped `strict<Security>`, silently
// bypassing the corpus.  The fix pins `as_public` explicitly.
template <typename Type>
using AsyncEndpoint = ::crucible::fixy::fn<Type,
    detail_stance::strict<dim::DimensionAxis::Refinement>,
    detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with_io,
    grant::as_public,
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
    detail_stance::strict<dim::DimensionAxis::Staleness>,
    detail_stance::strict<dim::DimensionAxis::Synchronization>>;

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
    grant::accept_default_strict_for<dim::DimensionAxis::Staleness>,
    grant::accept_default_strict_for<dim::DimensionAxis::Synchronization>>;

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

// FIXY-LAT-Security: every Security lattice point resolves to the
// matching substrate SecLevel.
static_assert(detail::resolve::project<grant::as_unclassified>::value
    == safety::fn::SecLevel::Unclassified,
    "grant::as_unclassified must project to SecLevel::Unclassified.");
static_assert(detail::resolve::project<grant::as_public>::value
    == safety::fn::SecLevel::Public,
    "grant::as_public must project to SecLevel::Public.");
static_assert(detail::resolve::project<grant::as_internal>::value
    == safety::fn::SecLevel::Internal,
    "grant::as_internal must project to SecLevel::Internal.");
static_assert(detail::resolve::project<grant::as_classified>::value
    == safety::fn::SecLevel::Classified,
    "grant::as_classified must project to SecLevel::Classified.");
static_assert(detail::resolve::project<grant::as_secret>::value
    == safety::fn::SecLevel::Secret,
    "grant::as_secret must project to SecLevel::Secret.");

// FIXY-LAT-Trust: every Trust lattice point resolves to the matching
// safety::trust::* tag.
static_assert(std::is_same_v<
    detail::resolve::project<grant::trust_verified>::type,
    safety::trust::Verified>,
    "grant::trust_verified must project to safety::trust::Verified.");
static_assert(std::is_same_v<
    detail::resolve::project<grant::trust_tested>::type,
    safety::trust::Tested>,
    "grant::trust_tested must project to safety::trust::Tested.");
static_assert(std::is_same_v<
    detail::resolve::project<grant::trust_unverified>::type,
    safety::trust::Unverified>,
    "grant::trust_unverified must project to safety::trust::Unverified.");
static_assert(std::is_same_v<
    detail::resolve::project<grant::trust_external>::type,
    safety::trust::External>,
    "grant::trust_external must project to safety::trust::External.");

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
    grant::accept_default_strict_for<dim::DimensionAxis::Staleness>,
    grant::accept_default_strict_for<dim::DimensionAxis::Synchronization>>(42);
static_assert(minted.value() == 42,
    "mint_fn must construct fixy::fn carrying the supplied value.");

}  // namespace detail::fn_self_test

}  // namespace crucible::fixy
