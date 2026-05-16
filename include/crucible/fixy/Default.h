#pragma once

// ── crucible::fixy — Default.h (FIXY-A2a) ──────────────────────────────
//
// The per-dim STRICT-DEFAULT catalog.  For every one of the 20 fixy
// dims this header declares a `strict_default_for<D>` specialization
// whose nested `type` (and where applicable `value`) names the
// substrate's shipping default grade.
//
// **The load-bearing observation.**  Crucible's substrate `safety::fn::Fn`
// already ships the FX-strict defaults (Linear / Row<> / Classified /
// None / Static / FromInternal / Verified / Opaque / Unstated / Exact /
// Zero / Trap / Immutable / NonReentrant / Unstated / 1 / Fresh) per
// misc/fixy.md §24.1.  fixy does NOT flip defaults — fixy enforces
// EXPLICIT engagement on every dim, even when the engagement is
// "accept the strict default".  This header is the catalog of what
// "strict" means; the engagement is enforced by fixy/Reject.h.
//
// **The no-skew invariant.**  The closing static_assert block reads
// `safety::fn::Fn<int>`'s per-axis aliases and pins each to our
// catalog entry.  A future maintainer who silently changes Fn's
// `Usage` default from Linear to Affine fires a build break here,
// flagging that fixy's "strict" terminology has drifted from the
// substrate's shipping defaults.
//
// ── Surface ────────────────────────────────────────────────────────────
//
//   template <dim::DimAxis D> struct strict_default_for;  // primary undefined
//
//   strict_default_for<dim::Usage>::value      == UsageMode::Linear
//   strict_default_for<dim::Usage>::dim_axis   == dim::Usage      (self-check)
//   strict_default_for<dim::Effect>::type      == effects::Row<>
//   strict_default_for<dim::Lifetime>::type    == lifetime::Static
//   ... 17 more ...
//
// For enum-valued dims (Usage / Security / Representation / Overflow /
// Mutation / Reentrancy) the specialization exposes a `value` member
// of the enum type plus a `using value_holder = integral_constant<...>`
// to admit type-level dispatch.  For type-valued dims (Refinement /
// Effect / Protocol / Lifetime / Provenance / Trust / Complexity /
// Precision / Space / Size / Staleness) only `type` is exposed.  Dim
// Type itself ships `using type = void` as a sentinel for "no default
// — binding must declare a concrete value type"; the engagement check
// in Reject.h still requires an explicit `accept_default_strict_for
// <dim::Type>` or a grant::from_type<T> tag — fixy refuses to invent a
// type for the author.  Dim Version ships `value = uint32_t{1}`.
// Dim Observability is DERIVED at the consumer site from EffectRow
// (Fn carries no dedicated slot) — we still expose a strict-default
// catalog entry so the engagement check in Reject.h treats it
// uniformly with the other 19 dims.
//
// ── Axiom coverage ────────────────────────────────────────────────────
//
//   InitSafe   — every specialization explicitly defines `type` and/or
//                `value`; no fallthrough to a defaulted-int member.
//   TypeSafe   — enum-valued strict defaults carry the substrate's
//                enum class; no implicit conversion admitted.
//   DetSafe    — pure consteval-callable surface; output identical
//                across compiles by reference equality to the
//                substrate symbols.
//   LeakSafe   — zero-state types; no resource.
//
// ── Runtime cost ──────────────────────────────────────────────────────
//
// Zero.  Specializations are pure compile-time metadata.  Catalog is
// inert metaprogramming machinery; no machine code emitted.
//
// ── References ────────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §2,§3       — Phase A scope
//   misc/fixy.md §24.1                  — strict default per dim
//   safety/Fn.h:295-315                 — substrate default list
//   CLAUDE.md §XVII                     — naming discipline

#include <crucible/effects/EffectRow.h>
#include <crucible/fixy/Dim.h>
#include <crucible/safety/Fn.h>

#include <cstdint>
#include <meta>
#include <type_traits>

namespace crucible::fixy::default_strict {

namespace fn = ::crucible::safety::fn;

// ── Primary template — undefined; specialization required ─────────────
template <dim::DimAxis D>
struct strict_default_for;

// ── Type-dim catalog ──────────────────────────────────────────────────

template <>
struct strict_default_for<dim::Type> {
    static constexpr dim::DimAxis dim_axis = dim::Type;
    using type = void;  // sentinel; author MUST provide the concrete value type
};

template <>
struct strict_default_for<dim::Refinement> {
    static constexpr dim::DimAxis dim_axis = dim::Refinement;
    using type = fn::pred::True;
};

template <>
struct strict_default_for<dim::Usage> {
    static constexpr dim::DimAxis dim_axis = dim::Usage;
    static constexpr fn::UsageMode value = fn::UsageMode::Linear;
    using value_holder = std::integral_constant<fn::UsageMode, value>;
};

template <>
struct strict_default_for<dim::Effect> {
    static constexpr dim::DimAxis dim_axis = dim::Effect;
    using type = ::crucible::effects::Row<>;
};

template <>
struct strict_default_for<dim::Security> {
    static constexpr dim::DimAxis dim_axis = dim::Security;
    static constexpr fn::SecLevel value = fn::SecLevel::Classified;
    using value_holder = std::integral_constant<fn::SecLevel, value>;
};

template <>
struct strict_default_for<dim::Protocol> {
    static constexpr dim::DimAxis dim_axis = dim::Protocol;
    using type = fn::proto::None;
};

template <>
struct strict_default_for<dim::Lifetime> {
    static constexpr dim::DimAxis dim_axis = dim::Lifetime;
    using type = fn::lifetime::Static;
};

template <>
struct strict_default_for<dim::Provenance> {
    static constexpr dim::DimAxis dim_axis = dim::Provenance;
    using type = ::crucible::safety::source::FromInternal;
};

template <>
struct strict_default_for<dim::Trust> {
    static constexpr dim::DimAxis dim_axis = dim::Trust;
    using type = ::crucible::safety::trust::Verified;
};

template <>
struct strict_default_for<dim::Representation> {
    static constexpr dim::DimAxis dim_axis = dim::Representation;
    static constexpr fn::ReprKind value = fn::ReprKind::Opaque;
    using value_holder = std::integral_constant<fn::ReprKind, value>;
};

template <>
struct strict_default_for<dim::Observability> {
    static constexpr dim::DimAxis dim_axis = dim::Observability;
    // Observability is DERIVED from EffectRow at the consumer site; the
    // strict-default catalog entry exists only so the engagement check
    // treats it uniformly with the other 19 dims.  No carrier type;
    // the sentinel type is `void` matching dim::Type's treatment.
    using type = void;
};

template <>
struct strict_default_for<dim::Complexity> {
    static constexpr dim::DimAxis dim_axis = dim::Complexity;
    using type = fn::cost::Unstated;
};

template <>
struct strict_default_for<dim::Precision> {
    static constexpr dim::DimAxis dim_axis = dim::Precision;
    using type = fn::precision::Exact;
};

template <>
struct strict_default_for<dim::Space> {
    static constexpr dim::DimAxis dim_axis = dim::Space;
    using type = fn::space::Zero;
};

template <>
struct strict_default_for<dim::Overflow> {
    static constexpr dim::DimAxis dim_axis = dim::Overflow;
    static constexpr fn::OverflowMode value = fn::OverflowMode::Trap;
    using value_holder = std::integral_constant<fn::OverflowMode, value>;
};

template <>
struct strict_default_for<dim::Mutation> {
    static constexpr dim::DimAxis dim_axis = dim::Mutation;
    static constexpr fn::MutationMode value = fn::MutationMode::Immutable;
    using value_holder = std::integral_constant<fn::MutationMode, value>;
};

template <>
struct strict_default_for<dim::Reentrancy> {
    static constexpr dim::DimAxis dim_axis = dim::Reentrancy;
    static constexpr fn::ReentrancyMode value = fn::ReentrancyMode::NonReentrant;
    using value_holder = std::integral_constant<fn::ReentrancyMode, value>;
};

template <>
struct strict_default_for<dim::Size> {
    static constexpr dim::DimAxis dim_axis = dim::Size;
    using type = fn::size_pol::Unstated;
};

template <>
struct strict_default_for<dim::Version> {
    static constexpr dim::DimAxis dim_axis = dim::Version;
    static constexpr std::uint32_t value = 1;
    using value_holder = std::integral_constant<std::uint32_t, value>;
};

template <>
struct strict_default_for<dim::Staleness> {
    static constexpr dim::DimAxis dim_axis = dim::Staleness;
    using type = fn::stale::Fresh;
};

// ── Catalog completeness self-test ────────────────────────────────────
//
// Reflection-driven coverage: every enumerator in dim::DimAxis must
// have a `strict_default_for` specialization, and every specialization
// must report its own dim_axis correctly.  A new substrate dim that
// lacks a fixy specialization here fires the build break.
namespace detail {

template <dim::DimAxis D>
inline constexpr bool has_strict_default_v =
    requires { strict_default_for<D>::dim_axis; }
    && (strict_default_for<D>::dim_axis == D);

[[nodiscard]] consteval bool every_dim_has_strict_default() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^dim::DimAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        // Splice in template-arg position requires parens per
        // feedback_gcc16_c26_reflection_gotchas (GCC 16.1.1).
        if (!has_strict_default_v<([:en:])>) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

}  // namespace detail

static_assert(detail::every_dim_has_strict_default(),
    "Every dim::DimAxis enumerator must have a "
    "fixy::default_strict::strict_default_for<D> specialization whose "
    "dim_axis member self-reports D.  A new substrate dim was added "
    "without coordinated fixy/Default.h update.");

// ── No-skew gate: substrate Fn defaults equal our catalog ─────────────
//
// `safety::fn::Fn<int>`'s per-axis aliases ARE the substrate's
// shipping defaults.  Pinning them to our catalog catches future
// drift where a substrate maintainer changes (e.g.) the Usage
// default from Linear to Affine without updating fixy.

static_assert(fn::Fn<int>::usage_v ==
              strict_default_for<dim::Usage>::value,
    "Substrate Fn default for Usage diverged from fixy strict-default "
    "catalog.  Investigate before bumping either side.");

static_assert(fn::Fn<int>::security_v ==
              strict_default_for<dim::Security>::value,
    "Substrate Fn default for Security diverged from fixy strict-"
    "default catalog.");

static_assert(fn::Fn<int>::repr_v ==
              strict_default_for<dim::Representation>::value,
    "Substrate Fn default for Representation diverged from fixy "
    "strict-default catalog.");

static_assert(fn::Fn<int>::overflow_v ==
              strict_default_for<dim::Overflow>::value,
    "Substrate Fn default for Overflow diverged from fixy strict-"
    "default catalog.");

static_assert(fn::Fn<int>::mutation_v ==
              strict_default_for<dim::Mutation>::value,
    "Substrate Fn default for Mutation diverged from fixy strict-"
    "default catalog.");

static_assert(fn::Fn<int>::reentrancy_v ==
              strict_default_for<dim::Reentrancy>::value,
    "Substrate Fn default for Reentrancy diverged from fixy strict-"
    "default catalog.");

static_assert(fn::Fn<int>::version_v ==
              strict_default_for<dim::Version>::value,
    "Substrate Fn default for Version diverged from fixy strict-"
    "default catalog.");

static_assert(std::is_same_v<fn::Fn<int>::refinement_t,
                             strict_default_for<dim::Refinement>::type>,
    "Substrate Fn default for Refinement diverged from fixy strict-"
    "default catalog.");

static_assert(std::is_same_v<fn::Fn<int>::effect_row_t,
                             strict_default_for<dim::Effect>::type>,
    "Substrate Fn default for Effect diverged from fixy strict-default "
    "catalog.");

static_assert(std::is_same_v<fn::Fn<int>::protocol_t,
                             strict_default_for<dim::Protocol>::type>,
    "Substrate Fn default for Protocol diverged from fixy strict-default "
    "catalog.");

static_assert(std::is_same_v<fn::Fn<int>::lifetime_t,
                             strict_default_for<dim::Lifetime>::type>,
    "Substrate Fn default for Lifetime diverged from fixy strict-"
    "default catalog.");

static_assert(std::is_same_v<fn::Fn<int>::source_t,
                             strict_default_for<dim::Provenance>::type>,
    "Substrate Fn default for Provenance diverged from fixy strict-"
    "default catalog.");

static_assert(std::is_same_v<fn::Fn<int>::trust_t,
                             strict_default_for<dim::Trust>::type>,
    "Substrate Fn default for Trust diverged from fixy strict-default "
    "catalog.");

static_assert(std::is_same_v<fn::Fn<int>::cost_t,
                             strict_default_for<dim::Complexity>::type>,
    "Substrate Fn default for Complexity diverged from fixy strict-"
    "default catalog.");

static_assert(std::is_same_v<fn::Fn<int>::precision_t,
                             strict_default_for<dim::Precision>::type>,
    "Substrate Fn default for Precision diverged from fixy strict-"
    "default catalog.");

static_assert(std::is_same_v<fn::Fn<int>::space_t,
                             strict_default_for<dim::Space>::type>,
    "Substrate Fn default for Space diverged from fixy strict-default "
    "catalog.");

static_assert(std::is_same_v<fn::Fn<int>::size_t_,
                             strict_default_for<dim::Size>::type>,
    "Substrate Fn default for Size diverged from fixy strict-default "
    "catalog.");

static_assert(std::is_same_v<fn::Fn<int>::staleness_t,
                             strict_default_for<dim::Staleness>::type>,
    "Substrate Fn default for Staleness diverged from fixy strict-"
    "default catalog.");

// ── Convenience alias for type-valued dims ────────────────────────────
template <dim::DimAxis D>
using strict_default_t = typename strict_default_for<D>::type;

}  // namespace crucible::fixy::default_strict
