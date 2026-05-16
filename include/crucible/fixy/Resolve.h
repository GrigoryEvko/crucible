#pragma once

// ── crucible::fixy — Resolve.h (FIXY-B1) ──────────────────────────────
//
// The load-bearing translator: a `Grants...` pack — already validated
// by `IsAccepted` (every dim engaged) — is mapped to the 19 substrate
// `safety::fn::Fn<...>` template parameters.  Each per-dim resolver
// walks the pack, finds the first tag whose `relaxes == D`, and
// either:
//
//   1. Extracts the tag's semantic value (relaxation tag), OR
//   2. Returns `strict_default_for<D>::value` (when the tag is the
//      `accept_default_strict_for<D>` explicit acknowledgement).
//
// Output: `resolved_fn_t<Type, Grants...>` — a `safety::fn::Fn<Type,
// /* 18 resolved params */>` instantiation that fixy::fn (FIXY-B2)
// wraps verbatim.
//
// ── Axes resolved (Fn parameter index 1..19) ─────────────────────────
//
//   1  Type            (required; from grant::typed<T> OR default_required sentinel)
//   2  Refinement      (grant::refined_with<Pred> | pred::True)
//   3  Usage           (grant::copy/affine/ghost/borrow/capability_usage | Linear)
//   4  EffectRow       (grant::with<Es...> | Row<>)
//   5  Security        (grant::declassify | grant::upgrade_to_secret | Classified)
//   6  Protocol        (grant::protocol_session<P> | proto::None)
//   7  Lifetime        (grant::lifetime_region<Tag> | lifetime::Static)
//   8  Provenance      (grant::from_source<S> | grant::sanitize<C> | source::FromInternal)
//   9  Trust           (grant::trust_assumed | grant::trust_assumed_for | trust::Verified)
//   10 Representation  (grant::repr_*, grant::vendor<V>, grant::tier<R> | ReprKind::Opaque)
//   11 (Observability — derived from EffectRow at consumer site; not in Fn slot)
//   12 Complexity      (grant::complexity_* | cost::Unstated)
//   13 Precision       (grant::precision_*, reassociate, precision_higham | Exact)
//   14 Space           (grant::space_bounded<N>, space_unbounded | Zero)
//   15 Overflow        (grant::overflow_wrap/saturate/widen | Trap)
//   16 Mutation        (grant::mutable_in_place/append_only/monotonic_advance | Immutable)
//   17 Reentrancy      (grant::reentrant | coroutine | NonReentrant)
//   18 Size            (grant::sized<D>/productive | Unstated)
//   19 Version         (grant::version<V> | 1)
//   20 Staleness       (grant::stale_to<τ> | stale::Fresh)
//
// ── Axiom coverage ───────────────────────────────────────────────────
//
//   InitSafe — every resolver returns a fully-specified default when
//              the Grants pack contains only accept_default_strict_for.
//   TypeSafe — strong enums + named types; no implicit conversions.
//   NullSafe — no pointers.
//   MemSafe  — zero-state metafunctions.
//   BorrowSafe — pure compile-time.
//   ThreadSafe — pure compile-time.
//   LeakSafe   — no resources.
//   DetSafe    — bit-identical metafunction output across compiles.
//
// ── Runtime cost ─────────────────────────────────────────────────────
//
// Zero.  Pure type-level dispatch.
//
// ── References ───────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §4 Phase B    — Fn aggregator + Rules
//   safety/Fn.h:295-315                   — 19-parameter Fn template
//   fixy/Grant.h                          — tag catalog being resolved
//   fixy/Default.h                        — strict-default fallback values

#include <crucible/effects/EffectRow.h>
#include <crucible/fixy/Default.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Reject.h>
#include <crucible/safety/Fn.h>

#include <cstdint>
#include <type_traits>

namespace crucible::fixy::resolve {

namespace fn = ::crucible::safety::fn;
namespace ds = ::crucible::fixy::default_strict;

// ═════════════════════════════════════════════════════════════════════
// ── pack_first_with_relaxes<D, Grants...> — find first matching ───
// ═════════════════════════════════════════════════════════════════════
//
// Returns the FIRST tag T in `Grants...` such that T::relaxes == D.
// If no match, returns `void` sentinel.  Subsequent resolvers dispatch
// on this match via std::is_same_v chains.

namespace detail {

template <dim::DimAxis D, typename... Grants>
struct pack_first;

template <dim::DimAxis D>
struct pack_first<D> {
    using type = void;
};

template <dim::DimAxis D, typename Head, typename... Rest>
struct pack_first<D, Head, Rest...> {
    using type = std::conditional_t<
        std::remove_cvref_t<Head>::relaxes == D,
        std::remove_cvref_t<Head>,
        typename pack_first<D, Rest...>::type>;
};

template <dim::DimAxis D, typename... Grants>
using pack_first_t = typename pack_first<D, Grants...>::type;

// Helper: is `T` an `accept_default_strict_for<D>` specialization?
template <typename T>
struct is_accept_tag : std::false_type {};

template <dim::DimAxis D>
struct is_accept_tag<accept_default_strict_for<D>> : std::true_type {};

template <typename T>
inline constexpr bool is_accept_v = is_accept_tag<T>::value;

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── Per-dim resolvers ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// ── 1. Type ──────────────────────────────────────────────────────────
//
// std::conditional_t evaluates BOTH branches eagerly — `typename
// accept_default_strict_for<D>::type` would fail substitution because
// the accept tag has no `type` member.  Use partial-specialization
// dispatch instead: the metafunction is specialized on the matched
// tag's structural shape.
namespace detail {

template <typename UserType, typename Matched>
struct resolve_type_dispatch {
    // Fallback: matched is grant::typed<T> → expose T.
    using type = typename Matched::type;
};

template <typename UserType, dim::DimAxis D>
struct resolve_type_dispatch<UserType, accept_default_strict_for<D>> {
    // Strict-default branch: dim::Type's sentinel is `default_required`,
    // but the user's actual `Type` template arg IS the carrier.
    using type = UserType;
};

}  // namespace detail

template <typename UserType, typename... Grants>
struct resolve_type {
    using type = typename detail::resolve_type_dispatch<
        UserType,
        detail::pack_first_t<dim::Type, Grants...>>::type;
};

template <typename UserType, typename... Grants>
using resolve_type_t = typename resolve_type<UserType, Grants...>::type;

// ── 2. Refinement ────────────────────────────────────────────────────
namespace detail {

template <typename Matched>
struct resolve_refinement_dispatch {
    // Fallback: matched is grant::refined_with<Pred> → expose Pred.
    using type = typename Matched::predicate;
};

template <dim::DimAxis D>
struct resolve_refinement_dispatch<accept_default_strict_for<D>> {
    using type = fn::pred::True;
};

}  // namespace detail

template <typename... Grants>
struct resolve_refinement {
    using type = typename detail::resolve_refinement_dispatch<
        detail::pack_first_t<dim::Refinement, Grants...>>::type;
};

template <typename... Grants>
using resolve_refinement_t = typename resolve_refinement<Grants...>::type;

// ── 3. Usage ─────────────────────────────────────────────────────────
template <typename... Grants>
[[nodiscard]] consteval fn::UsageMode resolve_usage() noexcept {
    using matched = detail::pack_first_t<dim::Usage, Grants...>;
    if constexpr (detail::is_accept_v<matched>)              return fn::UsageMode::Linear;
    else if constexpr (std::is_same_v<matched, grant::copy>)              return fn::UsageMode::Copy;
    else if constexpr (std::is_same_v<matched, grant::affine>)            return fn::UsageMode::Affine;
    else if constexpr (std::is_same_v<matched, grant::ghost>)             return fn::UsageMode::Ghost;
    else if constexpr (std::is_same_v<matched, grant::borrow>)            return fn::UsageMode::Borrow;
    else if constexpr (std::is_same_v<matched, grant::capability_usage>)  return fn::UsageMode::Capability;
    else return fn::UsageMode::Linear;  // unreachable (IsAccepted gated)
}

template <typename... Grants>
inline constexpr fn::UsageMode resolve_usage_v = resolve_usage<Grants...>();

// ── 4. EffectRow ─────────────────────────────────────────────────────
//
// grant::with<Es...> carries the row in its template parameters.  We
// detect via partial specialization on the template parameter pack.
namespace detail {

template <typename T>
struct effect_row_from_grant {
    using type = ::crucible::effects::Row<>;  // default
};

template <::crucible::effects::Effect... Es>
struct effect_row_from_grant<grant::with<Es...>> {
    using type = ::crucible::effects::Row<Es...>;
};

}  // namespace detail

template <typename... Grants>
struct resolve_effect_row {
private:
    using matched = detail::pack_first_t<dim::Effect, Grants...>;
public:
    using type = std::conditional_t<
        detail::is_accept_v<matched>,
        ::crucible::effects::Row<>,
        typename detail::effect_row_from_grant<matched>::type>;
};

template <typename... Grants>
using resolve_effect_row_t = typename resolve_effect_row<Grants...>::type;

// ── 5. Security ──────────────────────────────────────────────────────
template <typename... Grants>
[[nodiscard]] consteval fn::SecLevel resolve_security() noexcept {
    using matched = detail::pack_first_t<dim::Security, Grants...>;
    if constexpr (detail::is_accept_v<matched>)                      return fn::SecLevel::Classified;
    else if constexpr (std::is_same_v<matched, grant::upgrade_to_secret>) return fn::SecLevel::Secret;
    // declassify<Policy> → Public (default declass target; the Policy is
    // the audit trail, not the SecLevel value).  All declassify<...>
    // specializations map to Public.
    else return fn::SecLevel::Public;
}

template <typename... Grants>
inline constexpr fn::SecLevel resolve_security_v = resolve_security<Grants...>();

// ── 6. Protocol ──────────────────────────────────────────────────────
namespace detail {

template <typename Matched>
struct resolve_protocol_dispatch {
    // Fallback: grant::protocol_session<P> → P.
    using type = typename Matched::protocol;
};

template <dim::DimAxis D>
struct resolve_protocol_dispatch<accept_default_strict_for<D>> {
    using type = fn::proto::None;
};

}  // namespace detail

template <typename... Grants>
struct resolve_protocol {
    using type = typename detail::resolve_protocol_dispatch<
        detail::pack_first_t<dim::Protocol, Grants...>>::type;
};

template <typename... Grants>
using resolve_protocol_t = typename resolve_protocol<Grants...>::type;

// ── 7. Lifetime ──────────────────────────────────────────────────────
//
// grant::lifetime_region<Tag> carries `auto RegionTag` as NTTP — the
// resolver threads it through to `fn::lifetime::In<Tag>` so the
// downstream Fn carries the region identity.  accept_default_strict
// falls back to fn::lifetime::Static.
namespace detail {

template <typename T>
struct lifetime_from_grant { using type = fn::lifetime::Static; };

template <auto RegionTag>
struct lifetime_from_grant<grant::lifetime_region<RegionTag>> {
    using type = fn::lifetime::In<RegionTag>;
};

}  // namespace detail

template <typename... Grants>
struct resolve_lifetime {
private:
    using matched = detail::pack_first_t<dim::Lifetime, Grants...>;
public:
    using type = std::conditional_t<
        detail::is_accept_v<matched>,
        fn::lifetime::Static,
        typename detail::lifetime_from_grant<matched>::type>;
    static_assert(detail::is_accept_v<matched> ||
                  !std::is_same_v<matched, void>,
                  "Lifetime dim must engage via accept_default_strict_for or grant::lifetime_region.");
};

template <typename... Grants>
using resolve_lifetime_t = typename resolve_lifetime<Grants...>::type;

// ── 8. Provenance ────────────────────────────────────────────────────
template <typename... Grants>
struct resolve_provenance {
private:
    using matched = detail::pack_first_t<dim::Provenance, Grants...>;
public:
    using type = std::conditional_t<
        detail::is_accept_v<matched>,
        ::crucible::safety::source::FromInternal,
        // grant::from_source<S> → S; grant::sanitize<C> falls through to
        // FromInternal in Phase B (substrate-side sanitization tag TBD).
        typename detail::pack_first<dim::Provenance, Grants...>::type
    >;
};

// Provenance resolver: from_source<S> → S; sanitize<C> → source::Sanitized.
// Per FX §6, sanitization is a provenance-tag CHANGE (FromUser → Sanitized);
// the previous Phase B fallback dropped C silently and left the source at
// FromInternal — semantically wrong (a sanitized buffer was indistinguishable
// from an internally-sourced one). Sanitize now resolves to source::Sanitized
// uniformly across taint classes (the taint class itself is the audit-trail
// literal, NOT the destination source tag).
namespace detail {

template <typename T>
struct provenance_source { using type = ::crucible::safety::source::FromInternal; };

template <typename S>
struct provenance_source<grant::from_source<S>> { using type = S; };

template <typename TaintClass>
struct provenance_source<grant::sanitize<TaintClass>> {
    using type = ::crucible::safety::source::Sanitized;
};

}  // namespace detail

template <typename... Grants>
struct resolve_provenance_v2 {
private:
    using matched = detail::pack_first_t<dim::Provenance, Grants...>;
public:
    using type = std::conditional_t<
        detail::is_accept_v<matched>,
        ::crucible::safety::source::FromInternal,
        typename detail::provenance_source<matched>::type>;
};

template <typename... Grants>
using resolve_provenance_t = typename resolve_provenance_v2<Grants...>::type;

// ── 9. Trust ─────────────────────────────────────────────────────────
//
// Default is trust::Verified.  Relaxation tags (trust_assumed,
// trust_assumed_for<C>) downgrade to trust::Unverified.
template <typename... Grants>
struct resolve_trust {
private:
    using matched = detail::pack_first_t<dim::Trust, Grants...>;
public:
    using type = std::conditional_t<
        detail::is_accept_v<matched>,
        ::crucible::safety::trust::Verified,
        ::crucible::safety::trust::Unverified>;
};

template <typename... Grants>
using resolve_trust_t = typename resolve_trust<Grants...>::type;

// ── 10. Representation ───────────────────────────────────────────────
//
// repr_* tags map to the substrate ReprKind enum directly.  vendor<V>/
// tier<R>/vendor_backend<B>/recipe_tier<T>/transport_tier<T> retain
// Opaque at the substrate Repr slot — the vendor/tier/transport identity
// is exposed separately via `resolve_vendor_backend_v` /
// `resolve_recipe_tier_v` / `resolve_transport_tier_v` so downstream
// composition (mint_pipeline, Mimic backend selection) can read it
// without scraping the Grants pack.
template <typename... Grants>
[[nodiscard]] consteval fn::ReprKind resolve_repr() noexcept {
    using matched = detail::pack_first_t<dim::Representation, Grants...>;
    if constexpr (detail::is_accept_v<matched>)              return fn::ReprKind::Opaque;
    else if constexpr (std::is_same_v<matched, grant::repr_c>)       return fn::ReprKind::C;
    else if constexpr (std::is_same_v<matched, grant::repr_packed>)  return fn::ReprKind::Packed;
    else if constexpr (std::is_same_v<matched, grant::repr_aligned>) return fn::ReprKind::Aligned;
    else if constexpr (std::is_same_v<matched, grant::repr_simd>)    return fn::ReprKind::Simd;
    else if constexpr (std::is_same_v<matched, grant::repr_atomic>)  return fn::ReprKind::Atomic;
    // vendor / tier / transport tags retain Opaque at ReprKind; values
    // are exposed via the dedicated resolvers below.
    else return fn::ReprKind::Opaque;
}

template <typename... Grants>
inline constexpr fn::ReprKind resolve_repr_v = resolve_repr<Grants...>();

// ── Vendor / recipe-tier / transport-tier identity resolvers ─────────
//
// Walk the Grants pack to recover the typed-NTTP value for vendor /
// recipe / transport.  Each returns the substrate default (None /
// RELAXED / Loopback) when the binding accepts the strict default;
// otherwise the explicit NTTP from the grant.
//
// These are SEPARATE resolvers (not part of `resolved_fn_t`) because
// the substrate Fn slot is ReprKind-shaped — the typed identity lives
// in side metadata that flows into vendor-pinning composition outside
// the Fn aggregate.

namespace detail {

// Per-member "is-this-tag" + value extractor: primary template reports
// `present == false`; partial spec on the typed-NTTP grant reports the
// value.  Walks Grants and returns the FIRST present match (or the
// sentinel when none present).  Uses `present` instead of comparing
// against a default value because the typed enums admit their own
// default-equivalent (None / RELAXED / Loopback) as legitimate user
// choices that should NOT be confused with "no engagement".

template <typename T>
struct vendor_backend_of {
    static constexpr bool present = false;
    static constexpr ::crucible::algebra::lattices::VendorBackend value =
        ::crucible::algebra::lattices::VendorBackend::None;
};

template <::crucible::algebra::lattices::VendorBackend Backend>
struct vendor_backend_of<grant::vendor_backend<Backend>> {
    static constexpr bool present = true;
    static constexpr ::crucible::algebra::lattices::VendorBackend value = Backend;
};

template <typename T>
struct recipe_tier_of {
    static constexpr bool present = false;
    static constexpr ::crucible::algebra::lattices::Tolerance value =
        ::crucible::algebra::lattices::Tolerance::RELAXED;
};

template <::crucible::algebra::lattices::Tolerance T>
struct recipe_tier_of<grant::recipe_tier<T>> {
    static constexpr bool present = true;
    static constexpr ::crucible::algebra::lattices::Tolerance value = T;
};

template <typename T>
struct transport_tier_of {
    static constexpr bool present = false;
    static constexpr grant::TransportTier value = grant::TransportTier::Loopback;
};

template <grant::TransportTier T>
struct transport_tier_of<grant::transport_tier<T>> {
    static constexpr bool present = true;
    static constexpr grant::TransportTier value = T;
};

template <typename... Grants>
[[nodiscard]] consteval ::crucible::algebra::lattices::VendorBackend
    find_vendor_backend() noexcept {
    auto v = ::crucible::algebra::lattices::VendorBackend::None;
    bool found = false;
    (void)found;
    ((found
        ? v
        : (vendor_backend_of<std::remove_cvref_t<Grants>>::present
            ? (found = true,
               v = vendor_backend_of<std::remove_cvref_t<Grants>>::value)
            : v)), ...);
    return v;
}

template <typename... Grants>
[[nodiscard]] consteval ::crucible::algebra::lattices::Tolerance
    find_recipe_tier() noexcept {
    auto t = ::crucible::algebra::lattices::Tolerance::RELAXED;
    bool found = false;
    (void)found;
    ((found
        ? t
        : (recipe_tier_of<std::remove_cvref_t<Grants>>::present
            ? (found = true,
               t = recipe_tier_of<std::remove_cvref_t<Grants>>::value)
            : t)), ...);
    return t;
}

template <typename... Grants>
[[nodiscard]] consteval grant::TransportTier find_transport_tier() noexcept {
    auto t = grant::TransportTier::Loopback;
    bool found = false;
    (void)found;
    ((found
        ? t
        : (transport_tier_of<std::remove_cvref_t<Grants>>::present
            ? (found = true,
               t = transport_tier_of<std::remove_cvref_t<Grants>>::value)
            : t)), ...);
    return t;
}

}  // namespace detail

template <typename... Grants>
inline constexpr auto resolve_vendor_backend_v =
    detail::find_vendor_backend<Grants...>();

template <typename... Grants>
inline constexpr auto resolve_recipe_tier_v =
    detail::find_recipe_tier<Grants...>();

template <typename... Grants>
inline constexpr auto resolve_transport_tier_v =
    detail::find_transport_tier<Grants...>();

// "Was a typed-NTTP tag actually present in the pack?" — distinguishes
// "author chose None / RELAXED / Loopback explicitly" from "author
// engaged Representation via accept_default_strict_for".  Phase B/C
// vendor-pinning composition reads these before threading the typed
// identity downstream.

namespace detail {

template <typename... Grants>
[[nodiscard]] consteval bool any_vendor_backend_present() noexcept {
    return (false || ... || vendor_backend_of<std::remove_cvref_t<Grants>>::present);
}

template <typename... Grants>
[[nodiscard]] consteval bool any_recipe_tier_present() noexcept {
    return (false || ... || recipe_tier_of<std::remove_cvref_t<Grants>>::present);
}

template <typename... Grants>
[[nodiscard]] consteval bool any_transport_tier_present() noexcept {
    return (false || ... || transport_tier_of<std::remove_cvref_t<Grants>>::present);
}

}  // namespace detail

template <typename... Grants>
inline constexpr bool resolve_vendor_backend_present_v =
    detail::any_vendor_backend_present<Grants...>();

template <typename... Grants>
inline constexpr bool resolve_recipe_tier_present_v =
    detail::any_recipe_tier_present<Grants...>();

template <typename... Grants>
inline constexpr bool resolve_transport_tier_present_v =
    detail::any_transport_tier_present<Grants...>();

// ── Forge phase identity (Provenance axis) ───────────────────────────
//
// forge_phase<P> is one possible Provenance grant.  Walks the Grants
// pack and returns the first ForgePhase value; falls back to the
// sentinel `0xFF` cast-to-ForgePhase when no forge_phase tag is
// present.  Downstream consumers (mint_pipeline-shaped Forge
// composition) read this to verify P_i+1 = P_i + 1 across phase
// boundaries at compile time.

namespace detail {

template <typename T>
struct forge_phase_of {
    static constexpr bool present = false;
    static constexpr grant::ForgePhase value = static_cast<grant::ForgePhase>(0xFF);
};

template <grant::ForgePhase P>
struct forge_phase_of<grant::forge_phase<P>> {
    static constexpr bool present = true;
    static constexpr grant::ForgePhase value = P;
};

template <typename... Grants>
[[nodiscard]] consteval grant::ForgePhase find_forge_phase() noexcept {
    auto p = static_cast<grant::ForgePhase>(0xFF);
    bool found = false;
    (void)found;
    ((found
        ? p
        : (forge_phase_of<std::remove_cvref_t<Grants>>::present
            ? (found = true,
               p = forge_phase_of<std::remove_cvref_t<Grants>>::value)
            : p)), ...);
    return p;
}

}  // namespace detail

template <typename... Grants>
inline constexpr auto resolve_forge_phase_v =
    detail::find_forge_phase<Grants...>();

// Sentinel: 0xFF means "no forge_phase tag in the pack" — consumers
// check against this before reading the value.
inline constexpr grant::ForgePhase kNoForgePhase =
    static_cast<grant::ForgePhase>(0xFF);

// ── 12. Complexity ───────────────────────────────────────────────────
namespace detail {

template <typename T>
struct cost_from_grant { using type = fn::cost::Unstated; };

template <>
struct cost_from_grant<grant::complexity_constant> { using type = fn::cost::Constant; };

template <>
struct cost_from_grant<grant::complexity_unbounded> { using type = fn::cost::Unbounded; };

template <auto N>
struct cost_from_grant<grant::complexity_linear<N>> { using type = fn::cost::Linear<N>; };

template <auto N>
struct cost_from_grant<grant::complexity_quadratic<N>> { using type = fn::cost::Quadratic<N>; };

}  // namespace detail

template <typename... Grants>
struct resolve_cost {
private:
    using matched = detail::pack_first_t<dim::Complexity, Grants...>;
public:
    using type = std::conditional_t<
        detail::is_accept_v<matched>,
        fn::cost::Unstated,
        typename detail::cost_from_grant<matched>::type>;
};

template <typename... Grants>
using resolve_cost_t = typename resolve_cost<Grants...>::type;

// ── 13. Precision ────────────────────────────────────────────────────
namespace detail {

template <typename T>
struct precision_from_grant { using type = fn::precision::Exact; };

template <>
struct precision_from_grant<grant::precision_f32> { using type = fn::precision::F32; };

template <>
struct precision_from_grant<grant::precision_f64> { using type = fn::precision::F64; };

template <auto Bound>
struct precision_from_grant<grant::precision_higham<Bound>> {
    using type = fn::precision::Higham<Bound>;
};

}  // namespace detail

template <typename... Grants>
struct resolve_precision {
private:
    using matched = detail::pack_first_t<dim::Precision, Grants...>;
public:
    using type = std::conditional_t<
        detail::is_accept_v<matched>,
        fn::precision::Exact,
        typename detail::precision_from_grant<matched>::type>;
};

template <typename... Grants>
using resolve_precision_t = typename resolve_precision<Grants...>::type;

// ── 14. Space ────────────────────────────────────────────────────────
namespace detail {

template <typename T>
struct space_from_grant { using type = fn::space::Zero; };

template <>
struct space_from_grant<grant::space_unbounded> { using type = fn::space::Unbounded; };

template <auto N>
struct space_from_grant<grant::space_bounded<N>> { using type = fn::space::Bounded<N>; };

}  // namespace detail

template <typename... Grants>
struct resolve_space {
private:
    using matched = detail::pack_first_t<dim::Space, Grants...>;
public:
    using type = std::conditional_t<
        detail::is_accept_v<matched>,
        fn::space::Zero,
        typename detail::space_from_grant<matched>::type>;
};

template <typename... Grants>
using resolve_space_t = typename resolve_space<Grants...>::type;

// ── 15. Overflow ─────────────────────────────────────────────────────
template <typename... Grants>
[[nodiscard]] consteval fn::OverflowMode resolve_overflow() noexcept {
    using matched = detail::pack_first_t<dim::Overflow, Grants...>;
    if constexpr (detail::is_accept_v<matched>)                       return fn::OverflowMode::Trap;
    else if constexpr (std::is_same_v<matched, grant::overflow_wrap>)         return fn::OverflowMode::Wrap;
    else if constexpr (std::is_same_v<matched, grant::overflow_saturate>)     return fn::OverflowMode::Saturate;
    else if constexpr (std::is_same_v<matched, grant::overflow_widen>)        return fn::OverflowMode::Widen;
    else return fn::OverflowMode::Trap;
}

template <typename... Grants>
inline constexpr fn::OverflowMode resolve_overflow_v = resolve_overflow<Grants...>();

// ── 16. Mutation ─────────────────────────────────────────────────────
template <typename... Grants>
[[nodiscard]] consteval fn::MutationMode resolve_mutation() noexcept {
    using matched = detail::pack_first_t<dim::Mutation, Grants...>;
    if constexpr (detail::is_accept_v<matched>)                         return fn::MutationMode::Immutable;
    else if constexpr (std::is_same_v<matched, grant::mutable_in_place>)        return fn::MutationMode::Mutable;
    else if constexpr (std::is_same_v<matched, grant::append_only>)             return fn::MutationMode::Append;
    else if constexpr (std::is_same_v<matched, grant::monotonic_advance>)       return fn::MutationMode::Monotonic;
    else return fn::MutationMode::Immutable;
}

template <typename... Grants>
inline constexpr fn::MutationMode resolve_mutation_v = resolve_mutation<Grants...>();

// ── 17. Reentrancy ───────────────────────────────────────────────────
template <typename... Grants>
[[nodiscard]] consteval fn::ReentrancyMode resolve_reentrancy() noexcept {
    using matched = detail::pack_first_t<dim::Reentrancy, Grants...>;
    if constexpr (detail::is_accept_v<matched>)                  return fn::ReentrancyMode::NonReentrant;
    else if constexpr (std::is_same_v<matched, grant::reentrant>)       return fn::ReentrancyMode::Reentrant;
    else if constexpr (std::is_same_v<matched, grant::coroutine>)       return fn::ReentrancyMode::Coroutine;
    else return fn::ReentrancyMode::NonReentrant;
}

template <typename... Grants>
inline constexpr fn::ReentrancyMode resolve_reentrancy_v = resolve_reentrancy<Grants...>();

// ── 18. Size ─────────────────────────────────────────────────────────
namespace detail {

template <typename T>
struct size_from_grant { using type = fn::size_pol::Unstated; };

template <>
struct size_from_grant<grant::productive> { using type = fn::size_pol::Productive; };

template <auto Depth>
struct size_from_grant<grant::sized<Depth>> { using type = fn::size_pol::Sized<Depth>; };

}  // namespace detail

template <typename... Grants>
struct resolve_size_pol {
private:
    using matched = detail::pack_first_t<dim::Size, Grants...>;
public:
    using type = std::conditional_t<
        detail::is_accept_v<matched>,
        fn::size_pol::Unstated,
        typename detail::size_from_grant<matched>::type>;
};

template <typename... Grants>
using resolve_size_pol_t = typename resolve_size_pol<Grants...>::type;

// ── 19. Version ──────────────────────────────────────────────────────
namespace detail {

template <typename T>
struct version_from_grant { static constexpr std::uint32_t value = 1; };

template <std::uint32_t V>
struct version_from_grant<grant::version<V>> { static constexpr std::uint32_t value = V; };

}  // namespace detail

template <typename... Grants>
[[nodiscard]] consteval std::uint32_t resolve_version() noexcept {
    using matched = detail::pack_first_t<dim::Version, Grants...>;
    if constexpr (detail::is_accept_v<matched>) return std::uint32_t{1};
    else return detail::version_from_grant<matched>::value;
}

template <typename... Grants>
inline constexpr std::uint32_t resolve_version_v = resolve_version<Grants...>();

// ── 20. Staleness ────────────────────────────────────────────────────
namespace detail {

template <typename T>
struct staleness_from_grant { using type = fn::stale::Fresh; };

template <auto TauMax>
struct staleness_from_grant<grant::stale_to<TauMax>> {
    using type = fn::stale::Stale<TauMax>;
};

}  // namespace detail

template <typename... Grants>
struct resolve_staleness {
private:
    using matched = detail::pack_first_t<dim::Staleness, Grants...>;
public:
    using type = std::conditional_t<
        detail::is_accept_v<matched>,
        fn::stale::Fresh,
        typename detail::staleness_from_grant<matched>::type>;
};

template <typename... Grants>
using resolve_staleness_t = typename resolve_staleness<Grants...>::type;

// ═════════════════════════════════════════════════════════════════════
// ── resolved_fn_t — the full safety::fn::Fn<...> instantiation ─────
// ═════════════════════════════════════════════════════════════════════
//
// Synthesizes the 19 substrate parameters in Fn.h's exact order.
// Used by fixy::fn<Type, Grants...> as `underlying_fn_t`.

template <typename UserType, typename... Grants>
using resolved_fn_t = fn::Fn<
    resolve_type_t<UserType, Grants...>,    //  1 Type
    resolve_refinement_t<Grants...>,        //  2 Refinement
    resolve_usage_v<Grants...>,             //  3 Usage
    resolve_effect_row_t<Grants...>,        //  4 EffectRow
    resolve_security_v<Grants...>,          //  5 Security
    resolve_protocol_t<Grants...>,          //  6 Protocol
    resolve_lifetime_t<Grants...>,          //  7 Lifetime
    resolve_provenance_t<Grants...>,        //  8 Provenance
    resolve_trust_t<Grants...>,             //  9 Trust
    resolve_repr_v<Grants...>,              // 10 Representation
    resolve_cost_t<Grants...>,              // 11 Complexity (Fn slot 11; Observability is derived)
    resolve_precision_t<Grants...>,         // 12 Precision
    resolve_space_t<Grants...>,             // 13 Space
    resolve_overflow_v<Grants...>,          // 14 Overflow
    resolve_mutation_v<Grants...>,          // 15 Mutation
    resolve_reentrancy_v<Grants...>,        // 16 Reentrancy
    resolve_size_pol_t<Grants...>,          // 17 Size
    resolve_version_v<Grants...>,           // 18 Version
    resolve_staleness_t<Grants...>          // 19 Staleness
>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests: canonical-pack resolutions land on substrate defaults
// ═════════════════════════════════════════════════════════════════════

namespace self_test {

// All-strict-accept pack on `int` → Fn<int> with all Fn defaults.
using AllStrictResolution = resolved_fn_t<int,
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Usage>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>;

static_assert(std::is_same_v<AllStrictResolution, fn::Fn<int>>,
    "All-strict-accept on int must produce fn::Fn<int> with all substrate defaults.");

// Single-relaxation pack: grant::copy → Fn<int> with Usage = Copy.
using CopyResolution = resolved_fn_t<int,
    grant::copy,
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>;

static_assert(CopyResolution::usage_v == fn::UsageMode::Copy,
    "grant::copy must resolve to UsageMode::Copy.");

// Effect relaxation: grant::with<IO, Bg> → Fn::effect_row_t == Row<IO, Bg>.
using IoBgResolution = resolved_fn_t<int,
    grant::with<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Bg>,
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Usage>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>;

static_assert(std::is_same_v<typename IoBgResolution::effect_row_t,
                             ::crucible::effects::Row<::crucible::effects::Effect::IO,
                                                     ::crucible::effects::Effect::Bg>>,
    "grant::with<IO, Bg> must resolve to effects::Row<IO, Bg>.");

// ── Vendor / recipe / transport / forge_phase resolution pins ────────

namespace vlat = ::crucible::algebra::lattices;

// Empty-pack defaults: None / RELAXED / Loopback / kNoForgePhase.
static_assert(resolve_vendor_backend_v<>      == vlat::VendorBackend::None);
static_assert(resolve_recipe_tier_v<>         == vlat::Tolerance::RELAXED);
static_assert(resolve_transport_tier_v<>      == grant::TransportTier::Loopback);
static_assert(resolve_forge_phase_v<>         == kNoForgePhase);
static_assert(!resolve_vendor_backend_present_v<>);
static_assert(!resolve_recipe_tier_present_v<>);
static_assert(!resolve_transport_tier_present_v<>);

// grant::vendor_nv pack → NV; present_v true.
static_assert(resolve_vendor_backend_v<grant::vendor_nv> ==
              vlat::VendorBackend::NV);
static_assert(resolve_vendor_backend_present_v<grant::vendor_nv>);

// grant::tier_bitexact → Tolerance::BITEXACT.
static_assert(resolve_recipe_tier_v<grant::tier_bitexact> ==
              vlat::Tolerance::BITEXACT);
static_assert(resolve_recipe_tier_present_v<grant::tier_bitexact>);

// grant::transport_tier<AfXdp> → AfXdp.
static_assert(resolve_transport_tier_v<grant::transport_tier<grant::TransportTier::AfXdp>>
              == grant::TransportTier::AfXdp);

// grant::forge_phase<Lower> → Lower.
static_assert(resolve_forge_phase_v<grant::forge_phase<grant::ForgePhase::Lower>>
              == grant::ForgePhase::Lower);

// Mixed pack: vendor + tier in same Representation engagement — both
// resolvers find their respective tags despite sharing the Repr dim.
static_assert(resolve_vendor_backend_v<grant::vendor_nv, grant::tier_bitexact>
              == vlat::VendorBackend::NV);
static_assert(resolve_recipe_tier_v<grant::vendor_nv, grant::tier_bitexact>
              == vlat::Tolerance::BITEXACT);

// Sanitize remaps Provenance to source::Sanitized (bug fix from Phase B).
struct XssTaint {};
using SanitizedPack = resolved_fn_t<int,
    grant::sanitize<XssTaint>,
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Usage>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>;
static_assert(std::is_same_v<typename SanitizedPack::source_t,
                             ::crucible::safety::source::Sanitized>,
    "grant::sanitize<C> must resolve Provenance to source::Sanitized.");

// lifetime_region threads through to lifetime::In<Tag> (was Static).
struct ArenaTag {};
inline constexpr ArenaTag kArena{};
using ArenaLifetimePack = resolved_fn_t<int,
    grant::lifetime_region<kArena>,
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Usage>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>;
static_assert(std::is_same_v<typename ArenaLifetimePack::lifetime_t,
                             fn::lifetime::In<kArena>>,
    "grant::lifetime_region<Tag> must resolve Lifetime to lifetime::In<Tag>.");

}  // namespace self_test

}  // namespace crucible::fixy::resolve
