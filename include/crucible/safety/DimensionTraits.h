#pragma once

// ── crucible::safety — DimensionTraits.h (Phase 0 P0-3) ─────────────
//
// The Tier S / L / T / F / V dispatch vocabulary that `safety/Fn.h`
// (Phase 0 P0-1) and `safety/CollisionCatalog.h` (Phase 0 P0-2)
// dispatch on per fixy.md §24.1.  This header ships:
//
//   1. TierKind enum — the 5 composition-law families.
//   2. DimensionAxis enum — the 20 dimensions per fixy.md §24.1
//      (FX's 22 minus dim 12 Clock Domain and dim 17 FP Order; both
//      drops justified in fixy.md §24.1 + §24.14).
//   3. tier_of_axis() — fixy.md §24.1 hard-coded mapping.
//   4. SemiringGrade / LatticeGrade / TypestateGrade /
//      FoundationalGrade / VersionedGrade — the 5 concept families
//      asserting the structural shape a grade type must carry to
//      participate in its Tier's composition law.
//   5. tier_for_grade<G> — best-effort grade-to-Tier classification
//      based on which concept G satisfies.  Specializable.
//
// Per fixy.md §24.1 the Tier table is the authoritative source of
// truth: each dimension is classified at exactly ONE Tier.  Tier
// determines the COMPOSITION LAW used at par/seq sites:
//
//   Tier S — Commutative semiring (par=+, seq=*, 0 annihilator)
//   Tier L — Lattice with validity check (par=join, seq=meet, valid_D)
//   Tier T — Typestate (transitions; no par/seq composition)
//   Tier F — Foundational (bidirectional elaboration + concept gates)
//   Tier V — Versioned (consistency check at each site)
//
// The 5 concept families are STRUCTURAL — they assert that a grade
// type carries the operations needed for its Tier's composition law.
// Concepts overlap by design: QttSemiring satisfies BOTH SemiringGrade
// AND LatticeGrade because the underlying carrier supports both
// composition laws.  Tier classification picks WHICH composition is
// used at par/seq sites for a given dimension.
//
// ── Why not detect Tier from grade structure alone? ─────────────────
//
// The grade carrier may satisfy multiple Tier concepts (every Semiring
// is also a Lattice for our purposes).  The per-dimension Tier comes
// from the dimension's declaration in fixy.md §24.1, NOT from the
// grade's concept satisfaction.  `tier_for_grade` is a best-effort
// heuristic for cases where the dimension is unknown; the canonical
// path is `tier_of_axis(D)` for D : DimensionAxis.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe   — concept rejections produce structured static_assert
//                output at template-substitution time.
//   InitSafe   — every enum has a name function + reflection-driven
//                coverage assertion + sentinel-leak check.
//   DetSafe    — operations are constexpr / consteval; no runtime
//                nondeterminism path.
//   LeakSafe   — zero-state types; no resources.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero on the hot path.  Concept gates fire at template instantiation;
// `tier_kind_name` / `dimension_axis_name` / `tier_of_axis` are
// constexpr (callable from runtime smoke tests AND from the consteval
// reflection-driven coverage helpers — per the algebra/Lattice.h
// convention "MUST be constexpr, NOT consteval"); the variable-template
// surface compiles to immediate values under -O3.
//
// ── Extension policy ────────────────────────────────────────────────
//
// Adding a new dimension is a four-step structural change:
//
//   1. Append a new enumerator to `DimensionAxis` — APPEND-ONLY.
//      Inserting into the middle would change indices of subsequent
//      enumerators and break per-wrapper trait specializations that
//      cite the dim by value.
//   2. Add the arm to `dimension_axis_name`'s switch.
//   3. Add the arm to `tier_of_axis`'s switch.
//   4. Reflection-driven self-tests re-fire automatically; if the
//      enumerator is added without the matching arms, build fails
//      with a named assertion identifying which switch is incomplete.
//
// Per fixy.md §24.14 wall-clock dimensions (Energy / Latency / Power /
// WallClock / BitsTransferred) are PROHIBITED — the compiler cannot
// prove physical bounds; annotation-only dimensions create false
// guarantees.  CI guard: PR adding such a dimension blocked by the
// extension policy enforcer.
//
// ── References ─────────────────────────────────────────────────────
//
//   misc/fixy.md §24.1            — the 20-dimension grade vector
//   misc/fixy.md §24.14           — FX inheritance map (drop list)
//   misc/02_05_2026.md            — Phase 0 commitment (P0-3 row)
//   crucible/algebra/Lattice.h    — Lattice / Semiring concepts
//   crucible/algebra/GradedTrait.h — GradedWrapper concept

#include <crucible/algebra/GradedTrait.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/safety/AllocClass.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/RecipeSpec.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/Wait.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── TierKind — the 5 composition-law families ──────────────────────
// ═════════════════════════════════════════════════════════════════════

enum class TierKind : std::uint8_t {
    Semiring     = 0,  // Tier S — par=+, seq=*, 0 annihilator (15 dims)
    Lattice      = 1,  // Tier L — par=join, seq=meet, valid_D check (1 dim)
    Typestate    = 2,  // Tier T — transitions on state; no par/seq (1 dim)
    Foundational = 3,  // Tier F — bidirectional elaboration (2 dims)
    Versioned    = 4,  // Tier V — consistency check at each site (1 dim)
};

inline constexpr std::size_t TIER_KIND_COUNT =
    std::meta::enumerators_of(^^TierKind).size();

[[nodiscard]] constexpr std::string_view tier_kind_name(TierKind t) noexcept {
    switch (t) {
        case TierKind::Semiring:     return "Tier-S (Semiring)";
        case TierKind::Lattice:      return "Tier-L (Lattice)";
        case TierKind::Typestate:    return "Tier-T (Typestate)";
        case TierKind::Foundational: return "Tier-F (Foundational)";
        case TierKind::Versioned:    return "Tier-V (Versioned)";
        default:                     return std::string_view{"<unknown TierKind>"};
    }
}

// ═════════════════════════════════════════════════════════════════════
// ── DimensionAxis — the 20 dimensions per fixy.md §24.1 ────────────
// ═════════════════════════════════════════════════════════════════════
//
// ORDER must match fixy.md §24.1 reading order; APPEND-ONLY.  The
// FX-numbered comment beside each enumerator preserves the source
// dimension number from FX's 22-dim catalog (dims 12 Clock Domain
// and 17 FP Order are dropped per fixy.md §24.1; their absence
// here is structural, not accidental).

enum class DimensionAxis : std::uint8_t {
    Type           = 0,   // F  (FX dim 1)
    Refinement     = 1,   // F  (FX dim 2)
    Usage          = 2,   // S  (FX dim 3)
    Effect         = 3,   // S  (FX dim 4)
    Security       = 4,   // S  (FX dim 5)
    Protocol       = 5,   // T  (FX dim 6)
    Lifetime       = 6,   // S  (FX dim 7)
    Provenance     = 7,   // S  (FX dim 8)
    Trust          = 8,   // S  (FX dim 9)
    Representation = 9,   // L  (FX dim 10)
    Observability  = 10,  // S  (FX dim 11)
    // FX dim 12 Clock Domain dropped per fixy.md §24.1 — Crucible
    // does not synthesize Verilog.
    Complexity     = 11,  // S  (FX dim 13)
    Precision      = 12,  // S  (FX dim 14)
    Space          = 13,  // S  (FX dim 15)
    Overflow       = 14,  // S  (FX dim 16)
    // FX dim 17 FP Order dropped per fixy.md §24.1 — NumericalRecipe
    // pinning at the Mimic-emit layer subsumes it.
    Mutation       = 15,  // S  (FX dim 18)
    Reentrancy     = 16,  // S  (FX dim 19)
    Size           = 17,  // S  (FX dim 20)
    Version        = 18,  // V  (FX dim 21)
    Staleness      = 19,  // S  (FX dim 22)
};

inline constexpr std::size_t DIMENSION_AXIS_COUNT =
    std::meta::enumerators_of(^^DimensionAxis).size();

[[nodiscard]] constexpr std::string_view dimension_axis_name(DimensionAxis d) noexcept {
    switch (d) {
        case DimensionAxis::Type:           return "Type";
        case DimensionAxis::Refinement:     return "Refinement";
        case DimensionAxis::Usage:          return "Usage";
        case DimensionAxis::Effect:         return "Effect";
        case DimensionAxis::Security:       return "Security";
        case DimensionAxis::Protocol:       return "Protocol";
        case DimensionAxis::Lifetime:       return "Lifetime";
        case DimensionAxis::Provenance:     return "Provenance";
        case DimensionAxis::Trust:          return "Trust";
        case DimensionAxis::Representation: return "Representation";
        case DimensionAxis::Observability:  return "Observability";
        case DimensionAxis::Complexity:     return "Complexity";
        case DimensionAxis::Precision:      return "Precision";
        case DimensionAxis::Space:          return "Space";
        case DimensionAxis::Overflow:       return "Overflow";
        case DimensionAxis::Mutation:       return "Mutation";
        case DimensionAxis::Reentrancy:     return "Reentrancy";
        case DimensionAxis::Size:           return "Size";
        case DimensionAxis::Version:        return "Version";
        case DimensionAxis::Staleness:      return "Staleness";
        default:                            return std::string_view{"<unknown DimensionAxis>"};
    }
}

// ─── tier_of_axis — fixy.md §24.1 hard-coded mapping ───────────────
//
// SOURCE OF TRUTH for which Tier each dimension uses.  Specializing
// this is a fixy.md design change, not a substrate change — every
// production caller routes here for the dim → Tier mapping.

[[nodiscard]] constexpr TierKind tier_of_axis(DimensionAxis d) noexcept {
    switch (d) {
        case DimensionAxis::Type:
        case DimensionAxis::Refinement:
            return TierKind::Foundational;

        case DimensionAxis::Protocol:
            return TierKind::Typestate;

        case DimensionAxis::Representation:
            return TierKind::Lattice;

        case DimensionAxis::Version:
            return TierKind::Versioned;

        case DimensionAxis::Usage:
        case DimensionAxis::Effect:
        case DimensionAxis::Security:
        case DimensionAxis::Lifetime:
        case DimensionAxis::Provenance:
        case DimensionAxis::Trust:
        case DimensionAxis::Observability:
        case DimensionAxis::Complexity:
        case DimensionAxis::Precision:
        case DimensionAxis::Space:
        case DimensionAxis::Overflow:
        case DimensionAxis::Mutation:
        case DimensionAxis::Reentrancy:
        case DimensionAxis::Size:
        case DimensionAxis::Staleness:
            return TierKind::Semiring;

        default:
            // Unreachable per the exhaustive switch + DIMENSION_AXIS_COUNT
            // self-test, but every path must return.  Returning Semiring as
            // the fallthrough would silently mis-classify new axes; instead
            // we return a value whose name() flags the leak in diagnostics.
            return TierKind{0xFF};
    }
}

template <DimensionAxis D>
inline constexpr TierKind tier_of_axis_v = tier_of_axis(D);

// ═════════════════════════════════════════════════════════════════════
// ── 5 Tier concept families ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each concept characterizes the STRUCTURAL SHAPE a grade type has
// when used at its respective Tier.  Concepts overlap by design.
// The Tier classification per dimension comes from `tier_of_axis`,
// not from concept-satisfaction detection.

// SemiringGrade — Tier S grades carry semiring composition (add/mul,
// zero/one) on top of the lattice carrier.  Used at par/seq sites
// where + and · are the composition operations.
template <typename G>
concept SemiringGrade = algebra::Lattice<G> && algebra::Semiring<G>;

// LatticeGrade — Tier L grades carry lattice composition (join/meet)
// without requiring semiring add/mul.  Used at par/seq sites where
// par=join and seq=meet, with a per-element validity predicate
// (valid_D check) checked on every composition.
template <typename G>
concept LatticeGrade = algebra::Lattice<G>;

// TypestateGrade — Tier T grades are session-protocol types whose
// composition is transition-based (no par/seq lattice algebra).
// Detected via dual exposure of `state_type` and `transition_type`,
// the convention used pervasively by sessions/Session.h.  A type
// that merely looks lattice-shaped will not satisfy this; sessions
// are deliberately NOT graded (see Safety.h umbrella).
template <typename G>
concept TypestateGrade = requires {
    typename G::state_type;
    typename G::transition_type;
};

// FoundationalGrade — Tier F grades cover dim 1 (Type) and dim 2
// (Refinement).  Bare types satisfy this (any T is a foundational
// grade for the Type dimension).  Refinement predicates additionally
// ship `static constexpr bool check(value)`; both shapes admit here.
// Per-dimension narrower concepts (e.g., Refined predicate gates)
// further discriminate downstream.
template <typename G>
concept FoundationalGrade = std::is_object_v<G>;

// VersionedGrade — Tier V grades carry a compatibility predicate
// between version values.  Required for Tier V composition: at each
// par/seq site the runtime checks compatible(prev, next) before
// admitting the new grade.
template <typename G>
concept VersionedGrade = requires {
    typename G::element_type;
} && requires (typename G::element_type a, typename G::element_type b) {
    { G::compatible(a, b) } -> std::convertible_to<bool>;
};

// ═════════════════════════════════════════════════════════════════════
// ── tier_for_grade — best-effort Tier classification of a grade ────
// ═════════════════════════════════════════════════════════════════════
//
// Default rule, in priority order:
//   1. TypestateGrade  → Tier T
//   2. VersionedGrade  → Tier V
//   3. SemiringGrade   → Tier S  (every Semiring is also a Lattice;
//                                  the Semiring discriminator wins)
//   4. LatticeGrade    → Tier L  (pure Lattice without Semiring)
//   5. otherwise        → Tier F  (Type / Refinement catch-all)
//
// CALLERS should prefer `tier_of_axis(D)` when the dimension D is
// known.  This trait is for cases where only the grade type is
// available and the dimension classification is ambiguous.  A
// per-grade specialization of `tier_for_grade<G>` overrides the
// default rule.

template <typename G>
struct tier_for_grade {
    static constexpr TierKind value = []() consteval {
        if constexpr (TypestateGrade<G>)        return TierKind::Typestate;
        else if constexpr (VersionedGrade<G>)   return TierKind::Versioned;
        else if constexpr (SemiringGrade<G>)    return TierKind::Semiring;
        else if constexpr (LatticeGrade<G>)     return TierKind::Lattice;
        else                                    return TierKind::Foundational;
    }();
};

template <typename G>
inline constexpr TierKind tier_for_grade_v = tier_for_grade<G>::value;

// ─── dimension_tier — Tier classification of a GradedWrapper ──────
//
// For wrappers conforming to GradedWrapper, the Tier comes from the
// substrate's lattice_type per the heuristic above.  This is the
// "best-effort" path; per-wrapper exact Tier (matching fixy.md §24.1
// hard-coded mapping for the dim the wrapper covers) ships when the
// wrapper specializes its dimension via the future `wrapper_dimension`
// trait (Phase 1 P1-N).

template <algebra::GradedWrapper W>
inline constexpr TierKind dimension_tier_v =
    tier_for_grade_v<typename W::lattice_type>;

// ═════════════════════════════════════════════════════════════════════
// ── wrapper_dimension / verify_quadruple — exact wrapper table ──────
// ═════════════════════════════════════════════════════════════════════
//
// `dimension_tier_v<W>` above is intentionally heuristic: it derives a
// Tier from the lattice carrier's structural concept shape.  GAPS-091
// needs the inverse discipline: each shipped Graded-backed wrapper names
// the dimension it is meant to carry, then a consteval verifier checks
// the wrapper's (lattice, modality, tier) surface against that explicit
// declaration.  This table is deliberately small and exact; adding a new
// Graded-backed wrapper means adding one specialization here.

template <typename W>
struct wrapper_dimension;

template <typename W>
concept DimensionedGradedWrapper =
    algebra::GradedWrapper<std::remove_cvref_t<W>> &&
    requires { wrapper_dimension<std::remove_cvref_t<W>>::value; };

template <DimensionedGradedWrapper W>
inline constexpr DimensionAxis wrapper_dimension_v =
    wrapper_dimension<std::remove_cvref_t<W>>::value;

template <DimensionedGradedWrapper W>
inline constexpr TierKind wrapper_tier_v =
    tier_of_axis(wrapper_dimension_v<W>);

template <DimensionedGradedWrapper W>
using wrapper_lattice_t = typename std::remove_cvref_t<W>::lattice_type;

template <DimensionedGradedWrapper W>
inline constexpr algebra::ModalityKind wrapper_modality_v =
    std::remove_cvref_t<W>::modality;

template <typename T>
struct wrapper_dimension<Linear<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Usage> {};

template <auto Pred, typename T>
struct wrapper_dimension<Refined<Pred, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Refinement> {};

template <auto Pred, typename T>
struct wrapper_dimension<SealedRefined<Pred, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Refinement> {};

template <typename T, typename Tag>
struct wrapper_dimension<Tagged<T, Tag>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Provenance> {};

template <typename T>
struct wrapper_dimension<Secret<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Security> {};

template <typename T>
struct wrapper_dimension<Stale<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Staleness> {};

template <typename T, std::size_t N, typename Tag>
struct wrapper_dimension<TimeOrdered<T, N, Tag>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Representation> {};

template <typename T, typename Cmp>
struct wrapper_dimension<Monotonic<T, Cmp>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Mutation> {};

template <typename T, template <typename...> class Storage>
struct wrapper_dimension<AppendOnly<T, Storage>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Mutation> {};

template <HotPathTier_v Tier, typename T>
struct wrapper_dimension<HotPath<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Complexity> {};

template <DetSafeTier_v Tier, typename T>
struct wrapper_dimension<DetSafe<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Effect> {};

template <Tolerance Tier, typename T>
struct wrapper_dimension<NumericalTier<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Precision> {};

template <VendorBackend_v Backend, typename T>
struct wrapper_dimension<Vendor<Backend, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Representation> {};

template <ResidencyHeatTag_v Tier, typename T>
struct wrapper_dimension<ResidencyHeat<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Space> {};

template <CipherTierTag_v Tier, typename T>
struct wrapper_dimension<CipherTier<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Security> {};

template <AllocClassTag_v Tag, typename T>
struct wrapper_dimension<AllocClass<Tag, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Space> {};

template <WaitStrategy_v Strategy, typename T>
struct wrapper_dimension<Wait<Strategy, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Reentrancy> {};

template <MemOrderTag_v Tag, typename T>
struct wrapper_dimension<MemOrder<Tag, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Reentrancy> {};

template <ProgressClass_v Class, typename T>
struct wrapper_dimension<Progress<Class, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Complexity> {};

template <Consistency_v Level, typename T>
struct wrapper_dimension<Consistency<Level, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Version> {};

template <Lifetime_v Scope, typename T>
struct wrapper_dimension<OpaqueLifetime<Scope, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Lifetime> {};

template <CrashClass_v Class, typename T>
struct wrapper_dimension<Crash<Class, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Effect> {};

template <typename T>
struct wrapper_dimension<Budgeted<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Space> {};

template <typename T>
struct wrapper_dimension<EpochVersioned<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Version> {};

template <typename T>
struct wrapper_dimension<NumaPlacement<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Representation> {};

template <typename T>
struct wrapper_dimension<RecipeSpec<T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Precision> {};

template <TierKind Tier, typename Lattice>
[[nodiscard]] consteval bool tier_admits_lattice() noexcept {
    if constexpr (Tier == TierKind::Typestate) {
        return TypestateGrade<Lattice>;
    } else if constexpr (Tier == TierKind::Foundational) {
        return true;
    } else {
        // Tier-S / Tier-L / Tier-V wrappers in the current safety tree
        // are all lattice-backed at minimum.  Tier-S semiring laws are
        // enforced by the specific wrapper semantics where the carrier is
        // a type-level singleton or product lattice rather than by
        // requiring every grade carrier to publish add/mul.
        return LatticeGrade<Lattice>;
    }
}

template <TierKind, algebra::ModalityKind Modality>
[[nodiscard]] consteval bool tier_admits_modality() noexcept {
    return algebra::IsModality<Modality>;
}

template <DimensionedGradedWrapper W>
[[nodiscard]] consteval bool verify_quadruple() noexcept {
    using X = std::remove_cvref_t<W>;
    using L = wrapper_lattice_t<X>;
    constexpr auto tier = wrapper_tier_v<X>;
    constexpr auto modality = wrapper_modality_v<X>;

    return std::is_same_v<L, typename X::lattice_type>
        && std::is_same_v<L, typename X::graded_type::lattice_type>
        && modality == X::modality
        && modality == algebra::graded_modality_v<typename X::graded_type>
        && tier_kind_name(tier) != std::string_view{"<unknown TierKind>"}
        && tier_admits_lattice<tier, L>()
        && tier_admits_modality<tier, modality>();
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time + reflection-driven coverage) ──────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::dimension_traits_self_test {

// ── Cardinality assertions ─────────────────────────────────────────
static_assert(TIER_KIND_COUNT == 5,
    "TierKind catalog diverged from fixy.md §24.1 Tier S/L/T/F/V (5); "
    "if intentional, update fixy.md and this constant together.");
static_assert(DIMENSION_AXIS_COUNT == 20,
    "DimensionAxis catalog diverged from fixy.md §24.1 (20 dims after "
    "dropping FX dim 12 Clock Domain and dim 17 FP Order); if "
    "intentional, update fixy.md §24.1 + §24.14 and this constant.");

// ── Reflection-driven name coverage (TierKind) ─────────────────────
[[nodiscard]] consteval bool every_tier_kind_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^TierKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = tier_kind_name([:en:]);
        if (n == std::string_view{"<unknown TierKind>"}) return false;
        if (n.empty())                                   return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_tier_kind_has_name(),
    "tier_kind_name() missing arm for at least one TierKind — add the "
    "arm or the new tier leaks the '<unknown TierKind>' sentinel.");

// ── Reflection-driven name coverage (DimensionAxis) ────────────────
[[nodiscard]] consteval bool every_dimension_axis_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = dimension_axis_name([:en:]);
        if (n == std::string_view{"<unknown DimensionAxis>"}) return false;
        if (n.empty())                                        return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_dimension_axis_has_name(),
    "dimension_axis_name() missing arm for at least one DimensionAxis — "
    "add the arm or the new axis leaks the '<unknown DimensionAxis>' "
    "sentinel.");

// ── Reflection-driven Tier coverage (every axis maps to a Tier) ────
[[nodiscard]] consteval bool every_dimension_axis_has_tier() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto t = tier_of_axis([:en:]);
        // tier_of_axis returns TierKind{0xFF} on unreachable
        // fallthrough; the name resolves to "<unknown TierKind>".
        if (tier_kind_name(t) == std::string_view{"<unknown TierKind>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_dimension_axis_has_tier(),
    "tier_of_axis() switch missing arm for at least one DimensionAxis — "
    "add the arm or new axes silently fall through to the unreachable "
    "TierKind{0xFF} sentinel.");

// ── fixy.md §24.1 hard-coded Tier counts ──────────────────────────
[[nodiscard]] consteval std::size_t count_dims_in_tier(TierKind t) noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^DimensionAxis));
    std::size_t n = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (tier_of_axis([:en:]) == t) ++n;
    }
#pragma GCC diagnostic pop
    return n;
}

static_assert(count_dims_in_tier(TierKind::Semiring)     == 15,
    "fixy.md §24.1 declares 15 Tier-S dimensions; tier_of_axis disagrees.");
static_assert(count_dims_in_tier(TierKind::Lattice)      == 1,
    "fixy.md §24.1 declares 1 Tier-L dimension (Representation); "
    "tier_of_axis disagrees.");
static_assert(count_dims_in_tier(TierKind::Typestate)    == 1,
    "fixy.md §24.1 declares 1 Tier-T dimension (Protocol); "
    "tier_of_axis disagrees.");
static_assert(count_dims_in_tier(TierKind::Foundational) == 2,
    "fixy.md §24.1 declares 2 Tier-F dimensions (Type, Refinement); "
    "tier_of_axis disagrees.");
static_assert(count_dims_in_tier(TierKind::Versioned)    == 1,
    "fixy.md §24.1 declares 1 Tier-V dimension (Version); "
    "tier_of_axis disagrees.");

// Sum check — every dim assigned to exactly one Tier.
static_assert(count_dims_in_tier(TierKind::Semiring)
            + count_dims_in_tier(TierKind::Lattice)
            + count_dims_in_tier(TierKind::Typestate)
            + count_dims_in_tier(TierKind::Foundational)
            + count_dims_in_tier(TierKind::Versioned)
              == DIMENSION_AXIS_COUNT,
    "Sum of per-Tier dimension counts does not equal DIMENSION_AXIS_COUNT "
    "— a dimension is either uncounted or double-counted in tier_of_axis.");

// ── Concept witnesses ──────────────────────────────────────────────
//
// Trivial in-house witnesses for each Tier concept; exercises the
// concepts WITHOUT pulling in the full algebra/lattices/* tree.

struct TestLattice {
    using element_type = bool;
    [[nodiscard]] static constexpr bool bottom() noexcept { return false; }
    [[nodiscard]] static constexpr bool top()    noexcept { return true;  }
    [[nodiscard]] static constexpr bool leq (bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
};

struct TestSemiring {
    using element_type = bool;
    [[nodiscard]] static constexpr bool bottom() noexcept { return false; }
    [[nodiscard]] static constexpr bool top()    noexcept { return true;  }
    [[nodiscard]] static constexpr bool leq (bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
    [[nodiscard]] static constexpr bool zero() noexcept { return false; }
    [[nodiscard]] static constexpr bool one()  noexcept { return true;  }
    [[nodiscard]] static constexpr bool add(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool mul(bool a, bool b) noexcept { return a && b; }
};

struct TestVersioned {
    using element_type = std::uint32_t;
    [[nodiscard]] static constexpr bool compatible(std::uint32_t a, std::uint32_t b) noexcept {
        return a == b;
    }
};

struct TestTypestate {
    using state_type      = int;
    using transition_type = int;
};

struct TestBareFoundational {
    int payload{0};
};

// Concept satisfaction.
static_assert( LatticeGrade<TestLattice>);
static_assert(!SemiringGrade<TestLattice>);   // No add/mul.

static_assert( LatticeGrade<TestSemiring>);
static_assert( SemiringGrade<TestSemiring>);  // Both shapes.

static_assert( VersionedGrade<TestVersioned>);
static_assert(!LatticeGrade<TestVersioned>);
static_assert(!TypestateGrade<TestVersioned>);

static_assert( TypestateGrade<TestTypestate>);
static_assert(!LatticeGrade<TestTypestate>);
static_assert(!VersionedGrade<TestTypestate>);

static_assert(FoundationalGrade<int>);
static_assert(FoundationalGrade<TestBareFoundational>);

// tier_for_grade priority order.
static_assert(tier_for_grade_v<TestSemiring>          == TierKind::Semiring);
static_assert(tier_for_grade_v<TestLattice>           == TierKind::Lattice);
static_assert(tier_for_grade_v<TestTypestate>         == TierKind::Typestate);
static_assert(tier_for_grade_v<TestVersioned>         == TierKind::Versioned);
static_assert(tier_for_grade_v<TestBareFoundational>  == TierKind::Foundational);
static_assert(tier_for_grade_v<int>                   == TierKind::Foundational);

// Diagnostic surface — exact strings.
static_assert(tier_kind_name(TierKind::Semiring)     == "Tier-S (Semiring)");
static_assert(tier_kind_name(TierKind::Lattice)      == "Tier-L (Lattice)");
static_assert(tier_kind_name(TierKind::Typestate)    == "Tier-T (Typestate)");
static_assert(tier_kind_name(TierKind::Foundational) == "Tier-F (Foundational)");
static_assert(tier_kind_name(TierKind::Versioned)    == "Tier-V (Versioned)");

static_assert(dimension_axis_name(DimensionAxis::Type)           == "Type");
static_assert(dimension_axis_name(DimensionAxis::Refinement)     == "Refinement");
static_assert(dimension_axis_name(DimensionAxis::Usage)          == "Usage");
static_assert(dimension_axis_name(DimensionAxis::Effect)         == "Effect");
static_assert(dimension_axis_name(DimensionAxis::Security)       == "Security");
static_assert(dimension_axis_name(DimensionAxis::Protocol)       == "Protocol");
static_assert(dimension_axis_name(DimensionAxis::Lifetime)       == "Lifetime");
static_assert(dimension_axis_name(DimensionAxis::Provenance)     == "Provenance");
static_assert(dimension_axis_name(DimensionAxis::Trust)          == "Trust");
static_assert(dimension_axis_name(DimensionAxis::Representation) == "Representation");
static_assert(dimension_axis_name(DimensionAxis::Observability)  == "Observability");
static_assert(dimension_axis_name(DimensionAxis::Complexity)     == "Complexity");
static_assert(dimension_axis_name(DimensionAxis::Precision)      == "Precision");
static_assert(dimension_axis_name(DimensionAxis::Space)          == "Space");
static_assert(dimension_axis_name(DimensionAxis::Overflow)       == "Overflow");
static_assert(dimension_axis_name(DimensionAxis::Mutation)       == "Mutation");
static_assert(dimension_axis_name(DimensionAxis::Reentrancy)     == "Reentrancy");
static_assert(dimension_axis_name(DimensionAxis::Size)           == "Size");
static_assert(dimension_axis_name(DimensionAxis::Version)        == "Version");
static_assert(dimension_axis_name(DimensionAxis::Staleness)      == "Staleness");

// fixy.md §24.1 axis-to-Tier mapping spot checks.
static_assert(tier_of_axis(DimensionAxis::Type)           == TierKind::Foundational);
static_assert(tier_of_axis(DimensionAxis::Refinement)     == TierKind::Foundational);
static_assert(tier_of_axis(DimensionAxis::Usage)          == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Effect)         == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Protocol)       == TierKind::Typestate);
static_assert(tier_of_axis(DimensionAxis::Representation) == TierKind::Lattice);
static_assert(tier_of_axis(DimensionAxis::Version)        == TierKind::Versioned);
static_assert(tier_of_axis(DimensionAxis::Staleness)      == TierKind::Semiring);

// Variable-template form mirrors the function form.
static_assert(tier_of_axis_v<DimensionAxis::Type>     == TierKind::Foundational);
static_assert(tier_of_axis_v<DimensionAxis::Effect>   == TierKind::Semiring);
static_assert(tier_of_axis_v<DimensionAxis::Version>  == TierKind::Versioned);

// ── Wrapper × lattice × modality × tier verification ────────────────

struct QuadTag {};
using WLinear          = Linear<int>;
using WRefined         = Refined<positive, int>;
using WSealedRefined   = SealedRefined<positive, int>;
using WTagged          = Tagged<int, source::FromUser>;
using WSecret          = Secret<int>;
using WStale           = Stale<int>;
using WTimeOrdered     = TimeOrdered<int, 4, QuadTag>;
using WMonotonic       = Monotonic<std::uint64_t>;
using WAppendOnly      = AppendOnly<int>;
using WHotPath         = HotPath<HotPathTier_v::Hot, int>;
using WDetSafe         = DetSafe<DetSafeTier_v::Pure, int>;
using WNumericalTier   = NumericalTier<Tolerance::BITEXACT, int>;
using WVendor          = Vendor<VendorBackend_v::Portable, int>;
using WResidencyHeat   = ResidencyHeat<ResidencyHeatTag_v::Hot, int>;
using WCipherTier      = CipherTier<CipherTierTag_v::Hot, int>;
using WAllocClass      = AllocClass<AllocClassTag_v::Arena, int>;
using WWait            = Wait<WaitStrategy_v::SpinPause, int>;
using WMemOrder        = MemOrder<MemOrderTag_v::SeqCst, int>;
using WProgress        = Progress<ProgressClass_v::Bounded, int>;
using WConsistency     = Consistency<Consistency_v::STRONG, int>;
using WOpaqueLifetime  = OpaqueLifetime<Lifetime_v::PER_REQUEST, int>;
using WCrash           = Crash<CrashClass_v::NoThrow, int>;
using WBudgeted        = Budgeted<int>;
using WEpochVersioned  = EpochVersioned<int>;
using WNumaPlacement   = NumaPlacement<int>;
using WRecipeSpec      = RecipeSpec<int>;

static_assert(wrapper_tier_v<WLinear>         == TierKind::Semiring);
static_assert(wrapper_tier_v<WRefined>        == TierKind::Foundational);
static_assert(wrapper_tier_v<WTagged>         == TierKind::Semiring);
static_assert(wrapper_tier_v<WSecret>         == TierKind::Semiring);
static_assert(wrapper_tier_v<WTimeOrdered>    == TierKind::Lattice);
static_assert(wrapper_tier_v<WEpochVersioned> == TierKind::Versioned);

static_assert(verify_quadruple<WLinear>());
static_assert(verify_quadruple<WRefined>());
static_assert(verify_quadruple<WSealedRefined>());
static_assert(verify_quadruple<WTagged>());
static_assert(verify_quadruple<WSecret>());
static_assert(verify_quadruple<WStale>());
static_assert(verify_quadruple<WTimeOrdered>());
static_assert(verify_quadruple<WMonotonic>());
static_assert(verify_quadruple<WAppendOnly>());
static_assert(verify_quadruple<WHotPath>());
static_assert(verify_quadruple<WDetSafe>());
static_assert(verify_quadruple<WNumericalTier>());
static_assert(verify_quadruple<WVendor>());
static_assert(verify_quadruple<WResidencyHeat>());
static_assert(verify_quadruple<WCipherTier>());
static_assert(verify_quadruple<WAllocClass>());
static_assert(verify_quadruple<WWait>());
static_assert(verify_quadruple<WMemOrder>());
static_assert(verify_quadruple<WProgress>());
static_assert(verify_quadruple<WConsistency>());
static_assert(verify_quadruple<WOpaqueLifetime>());
static_assert(verify_quadruple<WCrash>());
static_assert(verify_quadruple<WBudgeted>());
static_assert(verify_quadruple<WEpochVersioned>());
static_assert(verify_quadruple<WNumaPlacement>());
static_assert(verify_quadruple<WRecipeSpec>());

}  // namespace detail::dimension_traits_self_test

}  // namespace crucible::safety
