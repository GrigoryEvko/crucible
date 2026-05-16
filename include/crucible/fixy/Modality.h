#pragma once

// ── crucible::fixy — Modality.h (FIXY-G10) ────────────────────────────
//
// Categorical modality classification for every grant tag.  Each
// shipped grant declares its modality via `using modality = ...;`;
// `grant_traits<G>` projects this to a value-level ModalityClass enum
// that the §6.8 collision catalog uses for modality-pair shape rules
// (R017, R018).
//
// ── The five modality classes ──────────────────────────────────────
//
//   Frame      — Absolute modality.  Invariant of the value; EBO-
//                collapses; does not transform or demand.  Examples:
//                reentrant, complexity_constant, sized<N>, copy.
//
//   Declares   — Comonad modality.  The binding PRODUCES a witness
//                for the property (counit-out).  Examples:
//                trust_assumed, from_source, declassify,
//                mutable_in_place, append_only, precision_*.
//
//   Requires   — RelativeMonad modality.  The binding DEMANDS an
//                input refinement from the caller (unit-in).
//                Examples: refined_with<Pred>, with<Effects...>,
//                overflow_wrap, overflow_saturate.
//
//   Linear     — Linear modality.  The binding consumes-and-produces
//                a resource (one-shot).  In Crucible's fixy grants
//                this maps specifically to `lifetime_region<Tag>`
//                paired with Mutable mutation — the binding holds an
//                exclusive borrow of the region for its execution.
//
//   Quotient   — Quotient modality.  Equivalence-class membership;
//                the grant names a representative of an equivalence
//                class (Version<N>, Vendor<V>, ForgePhase<P>, ...).
//                Two Quotient grants on the same axis with different
//                representatives are structurally incompatible.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   fixy::ModalityClass                          — enum class
//   fixy::classify_modality_v<Modality>          — algebra→fixy map
//   fixy::grant_traits<G>                        — projection trait
//   fixy::default_witness_for_class<MC>          — witness default per MC
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §6 Phase G    — G10 modality classification
//   algebra/Modality.h                    — ModalityKind enum
//   fixy/Grant.h                          — grant-side `using modality`
//   fixy/Rules.h                          — R017/R018 collision rules

#include <crucible/algebra/Modality.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Reject.h>
#include <crucible/safety/witness/Witness.h>

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── ModalityClass — the 5-class fixy taxonomy ──────────────────────
// ═════════════════════════════════════════════════════════════════════

enum class ModalityClass : std::uint8_t {
    Frame      = 0,  // Absolute       — invariant
    Declares   = 1,  // Comonad        — produces witness
    Requires   = 2,  // RelativeMonad  — demands refinement
    Linear     = 3,  // Linear         — consume-and-produce
    Quotient   = 4,  // Quotient       — equivalence class
    Coeffect   = 5,  // Coeffect       — resource consumption (G11)
};

[[nodiscard]] consteval std::string_view modality_class_name(
    ModalityClass mc) noexcept
{
    switch (mc) {
        case ModalityClass::Frame:    return "Frame";
        case ModalityClass::Declares: return "Declares";
        case ModalityClass::Requires: return "Requires";
        case ModalityClass::Linear:   return "Linear";
        case ModalityClass::Quotient: return "Quotient";
        case ModalityClass::Coeffect: return "Coeffect";
        default:                       return std::string_view{"<unknown ModalityClass>"};
    }
}

// ═════════════════════════════════════════════════════════════════════
// ── classify_modality_v — algebra::Modality* → ModalityClass ───────
// ═════════════════════════════════════════════════════════════════════
//
// Maps the algebra-layer modality tag types (Absolute_t, Comonad_t,
// RelativeMonad_t, Relative_t, Quotient_t) to the fixy ModalityClass
// enum.  The Linear modality class is reserved for Permission-typed
// resource transfer (grant::lifetime_region paired with Mutable —
// enforced by R013).

namespace detail {

template <typename Mod>
struct classify_modality_impl;

template <>
struct classify_modality_impl<::crucible::algebra::modality::Absolute_t> {
    static constexpr ModalityClass value = ModalityClass::Frame;
};

template <>
struct classify_modality_impl<::crucible::algebra::modality::Comonad_t> {
    static constexpr ModalityClass value = ModalityClass::Declares;
};

template <>
struct classify_modality_impl<::crucible::algebra::modality::RelativeMonad_t> {
    static constexpr ModalityClass value = ModalityClass::Requires;
};

template <>
struct classify_modality_impl<::crucible::algebra::modality::Relative_t> {
    // Cross-region flow — treat as Linear at the fixy layer (CSL
    // Permission discipline applies; R013 is the production rule).
    static constexpr ModalityClass value = ModalityClass::Linear;
};

template <>
struct classify_modality_impl<::crucible::algebra::modality::Quotient_t> {
    static constexpr ModalityClass value = ModalityClass::Quotient;
};

template <>
struct classify_modality_impl<::crucible::algebra::modality::Coeffect_t> {
    static constexpr ModalityClass value = ModalityClass::Coeffect;
};

}  // namespace detail

template <typename Mod>
inline constexpr ModalityClass classify_modality_v =
    detail::classify_modality_impl<Mod>::value;

// ═════════════════════════════════════════════════════════════════════
// ── grant_traits<G> — per-grant projection ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Reads G's `using modality = ...;` (FIXY-G10 sweep on Grant.h) and
// `using witness_t = ...;` (FIXY-G9 sweep), and exposes them under a
// uniform shape.

template <typename G>
struct grant_traits {
    using modality       = typename G::modality;
    using witness_t      = typename G::witness_t;
    static constexpr ModalityClass modality_class_v =
        classify_modality_v<modality>;
};

// ═════════════════════════════════════════════════════════════════════
// ── default_witness_for_class<MC> — suggested witness floor per MC ──
// ═════════════════════════════════════════════════════════════════════
//
// Each modality class has a per-class default witness expectation
// used by future migration tooling (FIXY-G16 nearest_canonical_t) to
// suggest evidence upgrades.  Not a hard constraint — bindings can
// default to Asserted regardless.

namespace detail {

template <ModalityClass MC>
struct default_witness_for_class_impl;

template <>
struct default_witness_for_class_impl<ModalityClass::Frame> {
    using type = ::crucible::safety::witness::Asserted<
        ::crucible::safety::witness::UnnamedRationale>;
};

template <>
struct default_witness_for_class_impl<ModalityClass::Declares> {
    using type = ::crucible::safety::witness::Tested<0>;
};

template <>
struct default_witness_for_class_impl<ModalityClass::Requires> {
    using type = ::crucible::safety::witness::Asserted<
        ::crucible::safety::witness::UnnamedRationale>;
};

template <>
struct default_witness_for_class_impl<ModalityClass::Linear> {
    using type = ::crucible::safety::witness::CrossValidated<0>;
};

template <>
struct default_witness_for_class_impl<ModalityClass::Quotient> {
    using type = ::crucible::safety::witness::Tested<0>;
};

template <>
struct default_witness_for_class_impl<ModalityClass::Coeffect> {
    // FIXY-G11: cost grades benefit from per-Cog calibration evidence.
    // Default-suggested witness floor is Tested (per the bench-suite
    // hooks that populate per-Cog OpcodeLatencyTable entries).  A bare
    // cost grant defaults to Asserted; the hot-path R015 rule demands
    // Tested for the high-residency paths.
    using type = ::crucible::safety::witness::Tested<0>;
};

}  // namespace detail

template <ModalityClass MC>
using default_witness_for_class =
    typename detail::default_witness_for_class_impl<MC>::type;

// ═════════════════════════════════════════════════════════════════════
// ── Followup A — same_region_tag_aliased_v (R017 tightening) ───────
// ═════════════════════════════════════════════════════════════════════
//
// R017's first cut rejected any two Linear-modality grants on
// dim::Lifetime regardless of region tag.  This was overconservative:
// `cg::lifetime_region<TagA, Mutable> + cg::lifetime_region<TagB,
// Mutable>` declares the binding holds TWO disjoint Permission tokens,
// which is structurally well-formed (CSL frame rule: disjoint
// permissions compose by separation).
//
// `same_region_tag_aliased_v<Grants...>` extracts the RegionTag NTTP
// from every Linear-modality grant in the pack and returns true iff
// two-or-more grants share the SAME tag.  This is what R017 actually
// needs to reject: two grants claiming exclusive permission to the
// SAME region.  Different-tag combinations now compile cleanly.

namespace detail {

// Extract the RegionTag NTTP from a Linear-modality grant.  Returns
// `std::optional`-style: present + region_tag set iff the grant has
// the lifetime_region<auto Tag> shape (or its evidenced variant).
template <typename G>
struct linear_region_tag {
    static constexpr bool present_v = false;
};

template <auto RegionTag>
struct linear_region_tag<::crucible::fixy::grant::lifetime_region<RegionTag>> {
    static constexpr bool present_v = true;
    static constexpr auto region_tag_v = RegionTag;
};

template <auto RegionTag, ::crucible::safety::witness::IsWitness W>
struct linear_region_tag<::crucible::fixy::grant::lifetime_region_e<RegionTag, W>> {
    static constexpr bool present_v = true;
    static constexpr auto region_tag_v = RegionTag;
};

// Equality check between two grants' RegionTag NTTPs.  Only true when
// BOTH grants carry a region_tag AND the tags are equal-by-NTTP.
template <typename G1, typename G2>
inline constexpr bool linear_region_tags_match_v = false;

template <typename G1, typename G2>
    requires (linear_region_tag<std::remove_cvref_t<G1>>::present_v &&
              linear_region_tag<std::remove_cvref_t<G2>>::present_v &&
              std::is_same_v<
                  decltype(linear_region_tag<std::remove_cvref_t<G1>>::region_tag_v),
                  decltype(linear_region_tag<std::remove_cvref_t<G2>>::region_tag_v)
              > &&
              (linear_region_tag<std::remove_cvref_t<G1>>::region_tag_v
               == linear_region_tag<std::remove_cvref_t<G2>>::region_tag_v))
inline constexpr bool linear_region_tags_match_v<G1, G2> = true;

// Count of grants in the pack that share the same RegionTag as G_pin.
template <typename G_pin, typename... Grants>
inline constexpr std::size_t count_same_region_v =
    (static_cast<std::size_t>(linear_region_tags_match_v<G_pin, Grants>) + ... + std::size_t{0});

// Pack-level same-tag aliasing predicate.  True iff some pair of grants
// in Grants... share the same RegionTag NTTP (count ≥ 2 for at least
// one pin).
template <typename... Grants>
inline constexpr bool same_region_tag_aliased_impl_v = false;

template <typename G, typename... Rest>
inline constexpr bool same_region_tag_aliased_impl_v<G, Rest...> =
    (linear_region_tag<std::remove_cvref_t<G>>::present_v &&
     count_same_region_v<G, Rest...> >= 1) ||
    same_region_tag_aliased_impl_v<Rest...>;

}  // namespace detail

// Public predicate: true iff Grants... contains two-or-more Linear-
// modality grants engaging the SAME RegionTag NTTP.
template <typename... Grants>
inline constexpr bool same_region_tag_aliased_v =
    detail::same_region_tag_aliased_impl_v<Grants...>;

// ═════════════════════════════════════════════════════════════════════
// ── Followup B — frame_declares_consistency_v (R018 teeth) ─────────
// ═════════════════════════════════════════════════════════════════════
//
// R018's first cut was a placeholder that always returned true because
// fn<>'s reject-by-default discipline forces exactly-one engagement per
// dim — the structural shape that R018 originally checked was already
// enforced upstream.  But stance::compose<Base, NewGrants...> paths can
// produce intermediate stance tuples where the invariant is not yet
// enforced; R018 needs teeth there.
//
// The predicate detects: for any dim axis, ≥1 grant with Frame modality
// AND ≥1 grant with Declares modality engaging the SAME axis is
// rejected.  Frame says "this property is INVARIANT of the value";
// Declares says "the binding PRODUCES the property".  Both claims on
// the same axis are categorically incompatible.

namespace detail {

// Per-grant predicate: does G engage Axis with modality class MC?
// Uses the engages_dim_v predicate from fixy/Reject.h (filtered to
// legitimate grant tags via grant_base inheritance) and looks up the
// grant's modality class via grant_traits<G>.
template <typename G, ::crucible::fixy::dim::DimAxis Axis,
          ::crucible::fixy::ModalityClass MC>
inline constexpr bool grant_engages_axis_with_class_v = false;

template <typename G, ::crucible::fixy::dim::DimAxis Axis,
          ::crucible::fixy::ModalityClass MC>
    requires (::crucible::fixy::detail::engages_dim_v<G, Axis>)
inline constexpr bool grant_engages_axis_with_class_v<G, Axis, MC> =
    (::crucible::fixy::grant_traits<std::remove_cvref_t<G>>::modality_class_v == MC);

// Per-axis collision: any Frame engagement AND any Declares engagement
// on the same axis.
template <::crucible::fixy::dim::DimAxis Axis, typename... Grants>
inline constexpr bool axis_has_frame_v =
    (grant_engages_axis_with_class_v<Grants, Axis, ModalityClass::Frame> || ... || false);

template <::crucible::fixy::dim::DimAxis Axis, typename... Grants>
inline constexpr bool axis_has_declares_v =
    (grant_engages_axis_with_class_v<Grants, Axis, ModalityClass::Declares> || ... || false);

template <::crucible::fixy::dim::DimAxis Axis, typename... Grants>
inline constexpr bool axis_has_frame_declares_collision_v =
    axis_has_frame_v<Axis, Grants...> && axis_has_declares_v<Axis, Grants...>;

// Fold over all 20 dim axes.  Returns true iff NO axis has both a
// Frame and a Declares engagement.  Used by stance::compose's
// requires clause AND by R018 in Rules.h.
template <typename... Grants>
[[nodiscard]] consteval bool frame_declares_consistent_consteval() noexcept {
    using ::crucible::fixy::dim::DimAxis;
    return !(axis_has_frame_declares_collision_v<DimAxis::Type,           Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Refinement,     Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Usage,          Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Effect,         Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Security,       Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Protocol,       Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Lifetime,       Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Provenance,     Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Trust,          Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Representation, Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Observability,  Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Complexity,     Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Precision,      Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Space,          Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Overflow,       Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Mutation,       Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Reentrancy,     Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Size,           Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Version,        Grants...> ||
             axis_has_frame_declares_collision_v<DimAxis::Staleness,      Grants...>);
}

}  // namespace detail

// Public predicate: true iff no axis has a Frame×Declares modality
// collision within Grants... .  Used by stance::compose's requires
// clause to fail at the compose call site (BEFORE the final fn<>
// aggregator) and by R018 in Rules.h to surface the same check as a
// queryable consumer-side predicate.
template <typename... Grants>
inline constexpr bool frame_declares_consistency_v =
    detail::frame_declares_consistent_consteval<Grants...>();

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace modality_self_test {

namespace alg = ::crucible::algebra;

static_assert(classify_modality_v<alg::modality::Absolute_t>      == ModalityClass::Frame);
static_assert(classify_modality_v<alg::modality::Comonad_t>       == ModalityClass::Declares);
static_assert(classify_modality_v<alg::modality::RelativeMonad_t> == ModalityClass::Requires);
static_assert(classify_modality_v<alg::modality::Relative_t>      == ModalityClass::Linear);
static_assert(classify_modality_v<alg::modality::Quotient_t>      == ModalityClass::Quotient);
static_assert(classify_modality_v<alg::modality::Coeffect_t>      == ModalityClass::Coeffect);

static_assert(modality_class_name(ModalityClass::Frame)    == "Frame");
static_assert(modality_class_name(ModalityClass::Declares) == "Declares");
static_assert(modality_class_name(ModalityClass::Requires) == "Requires");
static_assert(modality_class_name(ModalityClass::Linear)   == "Linear");
static_assert(modality_class_name(ModalityClass::Quotient) == "Quotient");
static_assert(modality_class_name(ModalityClass::Coeffect) == "Coeffect");

// ── Followup A — same_region_tag_aliased_v ─────────────────────────
//
// lifetime_region<0> + lifetime_region<1> are DIFFERENT region tags;
// the binding holds two disjoint permissions — compose cleanly.
static_assert(!same_region_tag_aliased_v<
    ::crucible::fixy::grant::lifetime_region<0>,
    ::crucible::fixy::grant::lifetime_region<1>>);

// lifetime_region<0> twice — SAME region tag; aliased; rejected.
static_assert(same_region_tag_aliased_v<
    ::crucible::fixy::grant::lifetime_region<0>,
    ::crucible::fixy::grant::lifetime_region<0>>);

// Evidenced variants follow the same rule.
using _T_witness = ::crucible::safety::witness::Tested<7>;
static_assert(same_region_tag_aliased_v<
    ::crucible::fixy::grant::lifetime_region<0>,
    ::crucible::fixy::grant::lifetime_region_e<0, _T_witness>>);

static_assert(!same_region_tag_aliased_v<
    ::crucible::fixy::grant::lifetime_region<0>,
    ::crucible::fixy::grant::lifetime_region_e<1, _T_witness>>);

// Empty pack and singleton pack — never aliased.
static_assert(!same_region_tag_aliased_v<>);
static_assert(!same_region_tag_aliased_v<
    ::crucible::fixy::grant::lifetime_region<0>>);

// Non-Linear grants in the pack don't contribute.
static_assert(!same_region_tag_aliased_v<
    ::crucible::fixy::grant::copy,
    ::crucible::fixy::grant::reentrant>);

// ── Followup B — frame_declares_consistency_v ─────────────────────
//
// Compatible packs: no Frame×Declares collision on any axis.
static_assert(frame_declares_consistency_v<
    ::crucible::fixy::grant::reentrant,            // Frame on Reentrancy
    ::crucible::fixy::grant::mutable_in_place>);   // Declares on Mutation

// Empty pack and singleton — always consistent.
static_assert(frame_declares_consistency_v<>);
static_assert(frame_declares_consistency_v<
    ::crucible::fixy::grant::reentrant>);

}  // namespace modality_self_test

}  // namespace crucible::fixy
