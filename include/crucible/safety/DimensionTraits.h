#pragma once

#include <crucible/algebra/GradedTrait.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/safety/AllocClass.h>
#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/Hw.h>
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
#include <crucible/safety/ScopedFence.h>
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/SimdWidthPinned.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/FpMode.h>
#include <crucible/safety/JoinPolicy.h>
#include <crucible/safety/Wait.h>
#include <crucible/safety/Witness.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::safety {

enum class TierKind : std::uint8_t {
    Semiring = 0,  // Tier S — par=+, seq=*, 0 annihilator (26 dims)
    Lattice = 1,  // Tier L — par=join, seq=meet, valid_D check (2 dims)
    Typestate = 2,  // Tier T — transitions on state; no par/seq (1 dim)
    Foundational = 3,  // Tier F — bidirectional elaboration (2 dims)
    Versioned = 4,  // Tier V — consistency check at each site (1 dim)
};

inline constexpr std::size_t TIER_KIND_COUNT = std::meta::enumerators_of(^^TierKind).size();

[[nodiscard]] constexpr std::string_view tier_kind_name(TierKind t) noexcept {
    switch (t) {
        case TierKind::Semiring:
            return "Tier-S (Semiring)";
        case TierKind::Lattice:
            return "Tier-L (Lattice)";
        case TierKind::Typestate:
            return "Tier-T (Typestate)";
        case TierKind::Foundational:
            return "Tier-F (Foundational)";
        case TierKind::Versioned:
            return "Tier-V (Versioned)";
        default:
            return std::string_view{"<unknown TierKind>"};
    }
}

// Append-only. Per-wrapper trait specializations cite an axis by value, so an
// enumerator inserted in the middle renumbers every axis after it. The
// parenthesised number on each arm is the source dimension in the FX catalog
// this vocabulary is derived from. Two of those dimensions are deliberately
// absent, marked where they would have fallen.
enum class DimensionAxis : std::uint8_t {
    Type = 0,  // F  (FX dim 1)
    Refinement = 1,  // F  (FX dim 2)
    Usage = 2,  // S  (FX dim 3)
    Effect = 3,  // S  (FX dim 4)
    Security = 4,  // S  (FX dim 5)
    Protocol = 5,  // T  (FX dim 6)
    Lifetime = 6,  // S  (FX dim 7)
    Provenance = 7,  // S  (FX dim 8)
    Trust = 8,  // S  (FX dim 9)
    Representation = 9,  // L  (FX dim 10)
    Observability = 10,  // S  (FX dim 11)
    // FX dim 12, clock domain, is absent. Crucible synthesizes no hardware.
    Complexity = 11,  // S  (FX dim 13)
    Precision = 12,  // S  (FX dim 14)
    Space = 13,  // S  (FX dim 15)
    Overflow = 14,  // S  (FX dim 16)
    // FX dim 17, floating-point order, is absent. Pinning the numerical recipe
    // at the emit layer already fixes the order.
    Mutation = 15,  // S  (FX dim 18)
    Reentrancy = 16,  // S  (FX dim 19)
    Size = 17,  // S  (FX dim 20)
    Version = 18,  // V  (FX dim 21)
    Staleness = 19,  // S  (FX dim 22)
    // Synchronization is not Reentrancy. Reentrancy tracks call-graph
    // self-call. A waiting strategy and a memory order say nothing about
    // self-call.
    Synchronization = 20,  // S  (Crucible extension)
    // Regime is not Complexity. Complexity tracks asymptotic and
    // termination-class bounds. Regime tracks where in the latency budget a
    // function runs. Neither subsumes the other: a cold function can still
    // terminate, and a hot one can still diverge.
    Regime = 21,  // S  (Crucible extension)
    // FpMode is not Precision. Precision tracks the element type. One element
    // type produces bit-different results under different rounding,
    // flush-to-zero and contraction modes, so the mode needs its own axis.
    FpMode = 22,  // S  (Crucible extension)
    // The effect row collapses files, network, mapping and process control
    // into one bit. This axis names the kernel-surface family instead, which
    // admission gates have to tell apart.
    SyscallSurface = 23,  // S  (Crucible extension)
    // Control-flow escape used to fold into the effect row. Call shape, stack
    // use, global state and standard-io had no axis at all.
    ControlFlow = 24,  // S  (Crucible extension)
    CallShape = 25,  // S  (Crucible extension)
    StackUse = 26,  // S  (Crucible extension)
    GlobalState = 27,  // S  (Crucible extension)
    Stdio = 28,  // S  (Crucible extension)
    HwInstruction = 29,  // S  (Crucible extension)
    // BarrierStrength is the standalone hardware-fence ladder, distinct from
    // the memory-order tag that lives on the Synchronization axis.
    BarrierStrength = 30,  // S  (Crucible extension)
    // SimdIsa and MemoryScope are Tier L rather than Tier S because each is a
    // non-distributive partial order: two vendor trunks that meet only at the
    // shared bottom and top.
    SimdIsa = 31,  // L  (Crucible extension)
    // MemoryScope is the visibility scope a publication reaches, where
    // BarrierStrength is the strength of the fence that publishes it. The two
    // compose by nesting the wrappers, never through one lattice operation.
    MemoryScope = 32,  // L  (Crucible extension)
};

inline constexpr std::size_t DIMENSION_AXIS_COUNT = std::meta::enumerators_of(^^DimensionAxis).size();

[[nodiscard]] constexpr std::string_view dimension_axis_name(DimensionAxis d) noexcept {
    switch (d) {
        case DimensionAxis::Type:
            return "Type";
        case DimensionAxis::Refinement:
            return "Refinement";
        case DimensionAxis::Usage:
            return "Usage";
        case DimensionAxis::Effect:
            return "Effect";
        case DimensionAxis::Security:
            return "Security";
        case DimensionAxis::Protocol:
            return "Protocol";
        case DimensionAxis::Lifetime:
            return "Lifetime";
        case DimensionAxis::Provenance:
            return "Provenance";
        case DimensionAxis::Trust:
            return "Trust";
        case DimensionAxis::Representation:
            return "Representation";
        case DimensionAxis::Observability:
            return "Observability";
        case DimensionAxis::Complexity:
            return "Complexity";
        case DimensionAxis::Precision:
            return "Precision";
        case DimensionAxis::Space:
            return "Space";
        case DimensionAxis::Overflow:
            return "Overflow";
        case DimensionAxis::Mutation:
            return "Mutation";
        case DimensionAxis::Reentrancy:
            return "Reentrancy";
        case DimensionAxis::Size:
            return "Size";
        case DimensionAxis::Version:
            return "Version";
        case DimensionAxis::Staleness:
            return "Staleness";
        case DimensionAxis::Synchronization:
            return "Synchronization";
        case DimensionAxis::Regime:
            return "Regime";
        case DimensionAxis::FpMode:
            return "FpMode";
        case DimensionAxis::SyscallSurface:
            return "SyscallSurface";
        case DimensionAxis::ControlFlow:
            return "ControlFlow";
        case DimensionAxis::CallShape:
            return "CallShape";
        case DimensionAxis::StackUse:
            return "StackUse";
        case DimensionAxis::GlobalState:
            return "GlobalState";
        case DimensionAxis::Stdio:
            return "Stdio";
        case DimensionAxis::HwInstruction:
            return "HwInstruction";
        case DimensionAxis::BarrierStrength:
            return "BarrierStrength";
        case DimensionAxis::SimdIsa:
            return "SimdIsa";
        case DimensionAxis::MemoryScope:
            return "MemoryScope";
        default:
            return std::string_view{"<unknown DimensionAxis>"};
    }
}

[[nodiscard]] constexpr TierKind tier_of_axis(DimensionAxis d) noexcept {
    switch (d) {
        case DimensionAxis::Type:
        case DimensionAxis::Refinement:
            return TierKind::Foundational;

        case DimensionAxis::Protocol:
            return TierKind::Typestate;

        case DimensionAxis::Representation:
        case DimensionAxis::SimdIsa:
        case DimensionAxis::MemoryScope:
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
        case DimensionAxis::Synchronization:
        case DimensionAxis::Regime:
        case DimensionAxis::FpMode:
        case DimensionAxis::SyscallSurface:
        case DimensionAxis::ControlFlow:
        case DimensionAxis::CallShape:
        case DimensionAxis::StackUse:
        case DimensionAxis::GlobalState:
        case DimensionAxis::Stdio:
        case DimensionAxis::HwInstruction:
        case DimensionAxis::BarrierStrength:
            return TierKind::Semiring;

        default:
            // Unreachable while the switch stays exhaustive. Returning
            // Semiring here would silently misclassify a newly added axis, so
            // return a value whose name reports the leak instead.
            return TierKind{0xFF};
    }
}

template <DimensionAxis D>
inline constexpr TierKind tier_of_axis_v = tier_of_axis(D);

// The five concepts below say what shape a grade carries, and they overlap on
// purpose. Which composition law a dimension actually uses comes from
// tier_of_axis, never from which of these a grade happens to satisfy.
template <typename G>
concept SemiringGrade = algebra::Lattice<G> && algebra::Semiring<G>;

template <typename G>
concept LatticeGrade = algebra::Lattice<G>;

// Detection keys on the pair of member types that session protocols expose.
// A merely lattice-shaped type does not satisfy this, and session types are
// deliberately not graded.
template <typename G>
concept TypestateGrade = requires {
    typename G::state_type;
    typename G::transition_type;
};

template <typename G>
concept FoundationalGrade = std::is_object_v<G>;

template <typename G>
concept VersionedGrade =
    requires { typename G::element_type; } && requires(typename G::element_type a, typename G::element_type b) {
        { G::compatible(a, b) } -> std::convertible_to<bool>;
    };

// Best-effort classification for a grade whose dimension is unknown. Prefer
// tier_of_axis whenever the dimension is known. The order of the tests is
// load-bearing: every semiring is also a lattice, so the semiring test has to
// run first or no grade would ever classify as Tier S.
template <typename G>
struct tier_for_grade {
    static constexpr TierKind value = []() consteval {
        if constexpr (TypestateGrade<G>)
            return TierKind::Typestate;
        else if constexpr (VersionedGrade<G>)
            return TierKind::Versioned;
        else if constexpr (SemiringGrade<G>)
            return TierKind::Semiring;
        else if constexpr (LatticeGrade<G>)
            return TierKind::Lattice;
        else
            return TierKind::Foundational;
    }();
};

template <typename G>
inline constexpr TierKind tier_for_grade_v = tier_for_grade<G>::value;

// Heuristic path, for a wrapper that has not declared its dimension. A wrapper
// that has one in the table below gets its exact Tier from wrapper_tier_v.
template <algebra::GradedWrapper W>
inline constexpr TierKind dimension_tier_v = tier_for_grade_v<typename W::lattice_type>;

// The table below runs the other way to the heuristic above: each wrapper
// names the dimension it is meant to carry, and verify_quadruple checks the
// wrapper's lattice, modality and tier against that declaration. One
// specialization per Graded-backed wrapper, no more.
template <typename W>
struct wrapper_dimension;

template <typename W>
concept DimensionedGradedWrapper =
    algebra::GradedWrapper<std::remove_cvref_t<W>> && requires { wrapper_dimension<std::remove_cvref_t<W>>::value; };

template <DimensionedGradedWrapper W>
inline constexpr DimensionAxis wrapper_dimension_v = wrapper_dimension<std::remove_cvref_t<W>>::value;

template <DimensionedGradedWrapper W>
inline constexpr TierKind wrapper_tier_v = tier_of_axis(wrapper_dimension_v<W>);

template <DimensionedGradedWrapper W>
using wrapper_lattice_t = typename std::remove_cvref_t<W>::lattice_type;

template <DimensionedGradedWrapper W>
inline constexpr algebra::ModalityKind wrapper_modality_v = std::remove_cvref_t<W>::modality;

template <typename T>
struct wrapper_dimension<Linear<T>> : std::integral_constant<DimensionAxis, DimensionAxis::Usage> {};

template <auto Pred, typename T>
struct wrapper_dimension<Refined<Pred, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Refinement> {};

template <auto Pred, typename T>
struct wrapper_dimension<SealedRefined<Pred, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Refinement> {};

template <typename T, typename Tag>
struct wrapper_dimension<Tagged<T, Tag>> : std::integral_constant<DimensionAxis, DimensionAxis::Provenance> {};

template <typename T>
struct wrapper_dimension<Secret<T>> : std::integral_constant<DimensionAxis, DimensionAxis::Security> {};

template <typename T>
struct wrapper_dimension<Stale<T>> : std::integral_constant<DimensionAxis, DimensionAxis::Staleness> {};

template <typename T, std::size_t N, typename Tag>
struct wrapper_dimension<TimeOrdered<T, N, Tag>>
    : std::integral_constant<DimensionAxis, DimensionAxis::Representation> {};

template <typename T, typename Cmp>
struct wrapper_dimension<Monotonic<T, Cmp>> : std::integral_constant<DimensionAxis, DimensionAxis::Mutation> {};

template <typename T, template <typename...> class Storage>
struct wrapper_dimension<AppendOnly<T, Storage>> : std::integral_constant<DimensionAxis, DimensionAxis::Mutation> {};

// Regime, not Complexity: Complexity carries termination class, which Progress
// occupies, and a hot function may still diverge.
template <HotPathTier_v Tier, typename T>
struct wrapper_dimension<HotPath<Tier, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Regime> {};

template <DetSafeTier_v Tier, typename T>
struct wrapper_dimension<DetSafe<Tier, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Effect> {};

template <Tolerance Tier, typename T>
struct wrapper_dimension<NumericalTier<Tier, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Precision> {};

template <VendorBackend_v Backend, typename T>
struct wrapper_dimension<Vendor<Backend, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Representation> {};

template <HwInstruction_v Tier, typename T>
struct wrapper_dimension<Hw<Tier, T>> : std::integral_constant<DimensionAxis, DimensionAxis::HwInstruction> {};

// BarrierStrength, not Synchronization: this is the fence strength a value was
// released under, not the memory-order tag on the operation.
template <BarrierStrength_v Tier, typename T>
struct wrapper_dimension<BarrierGuarded<Tier, T>>
    : std::integral_constant<DimensionAxis, DimensionAxis::BarrierStrength> {};

template <SimdIsa_v W, typename T>
struct wrapper_dimension<SimdWidthPinned<W, T>> : std::integral_constant<DimensionAxis, DimensionAxis::SimdIsa> {};

// MemoryScope, not BarrierStrength: this pins how far a publication is
// visible, not how strong the fence that published it was.
template <MemoryScope_v S, typename T>
struct wrapper_dimension<ScopedFence<S, T>> : std::integral_constant<DimensionAxis, DimensionAxis::MemoryScope> {};

template <ResidencyHeatTag_v Tier, typename T>
struct wrapper_dimension<ResidencyHeat<Tier, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Space> {};

template <CipherTierTag_v Tier, typename T>
struct wrapper_dimension<CipherTier<Tier, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Security> {};

template <AllocClassTag_v Tag, typename T>
struct wrapper_dimension<AllocClass<Tag, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Space> {};

// Synchronization, not Reentrancy: both are coordination choices and neither
// says anything about self-call.
template <WaitStrategy_v Strategy, typename T>
struct wrapper_dimension<Wait<Strategy, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Synchronization> {};

template <MemOrderTag_v Tag, typename T>
struct wrapper_dimension<MemOrder<Tag, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Synchronization> {};

template <ProgressClass_v Class, typename T>
struct wrapper_dimension<Progress<Class, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Complexity> {};

template <Consistency_v Level, typename T>
struct wrapper_dimension<Consistency<Level, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Version> {};

// Observability: a witness records how strong the evidence for the value's
// invariant is, which is an observation about the value rather than a property
// of it.
template <Witness_v Tier, typename T>
struct wrapper_dimension<Witness<Tier, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Observability> {};

// Synchronization: the discipline a parent applied to its spawned children.
// It shares the axis with the queue-side and memory-side sync wrappers.
template <JoinPolicy_v Tier, typename T>
struct wrapper_dimension<JoinPolicy<Tier, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Synchronization> {
};

// Every per-mode spelling instantiates this one class template with a
// different enumeration as its non-type argument, so this single
// specialization covers all of them and they all report the same axis.
template <auto Mode, typename T>
struct wrapper_dimension<FpModePinned<Mode, T>> : std::integral_constant<DimensionAxis, DimensionAxis::FpMode> {};

template <Lifetime_v Scope, typename T>
struct wrapper_dimension<OpaqueLifetime<Scope, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Lifetime> {};

template <CrashClass_v Class, typename T>
struct wrapper_dimension<Crash<Class, T>> : std::integral_constant<DimensionAxis, DimensionAxis::Effect> {};

template <typename T>
struct wrapper_dimension<Budgeted<T>> : std::integral_constant<DimensionAxis, DimensionAxis::Space> {};

template <typename T>
struct wrapper_dimension<EpochVersioned<T>> : std::integral_constant<DimensionAxis, DimensionAxis::Version> {};

template <typename T>
struct wrapper_dimension<NumaPlacement<T>> : std::integral_constant<DimensionAxis, DimensionAxis::Representation> {};

template <typename T>
struct wrapper_dimension<RecipeSpec<T>> : std::integral_constant<DimensionAxis, DimensionAxis::Precision> {};

template <TierKind Tier, typename Lattice>
[[nodiscard]] consteval bool tier_admits_lattice() noexcept {
    if constexpr (Tier == TierKind::Typestate) {
        return TypestateGrade<Lattice>;
    } else if constexpr (Tier == TierKind::Foundational) {
        return true;
    } else {
        // Tier S gates on LatticeGrade, not on SemiringGrade, and the leniency
        // is deliberate. Most Tier-S wrappers pin a one-element carrier, where
        // add and mul are degenerate and no semiring laws are published.
        // Semiring structure belongs to the chain the singleton is drawn from,
        // not to the singleton. Demanding SemiringGrade here would reject that
        // whole population. The strict variant below is the opt-in check for
        // the wrappers whose carrier really is the full semiring, and the
        // semiring laws themselves are verified at the carrier's own self-test.
        return LatticeGrade<Lattice>;
    }
}

// Opt-in strict variant for the wrappers whose carrier is the full semiring.
// verify_quadruple stays on the tolerant check.
template <TierKind Tier, typename Lattice>
[[nodiscard]] consteval bool tier_admits_semiring() noexcept {
    if constexpr (Tier == TierKind::Semiring) {
        return SemiringGrade<Lattice>;
    } else {
        return tier_admits_lattice<Tier, Lattice>();
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

    return std::is_same_v<L, typename X::lattice_type> && std::is_same_v<L, typename X::graded_type::lattice_type>
        && modality == X::modality && modality == algebra::graded_modality_v<typename X::graded_type>
        && tier_kind_name(tier) != std::string_view{"<unknown TierKind>"} && tier_admits_lattice<tier, L>()
        && tier_admits_modality<tier, modality>();
}

// The reverse of wrapper_dimension: which wrappers declare themselves on a
// given axis. The enumerators name wrapper templates rather than concrete
// instantiations, because the wrappers take heterogeneous non-type arguments
// and a list of concrete types would need a sentinel probe for each parametric
// one. Every wrapper_dimension specialization needs an enumerator here and an
// arm in each switch below.
enum class WrapperKind : std::uint8_t {
    Linear,  // → DimensionAxis::Usage
    Refined,  // → DimensionAxis::Refinement
    SealedRefined,  // → DimensionAxis::Refinement
    Tagged,  // → DimensionAxis::Provenance
    Secret,  // → DimensionAxis::Security
    Stale,  // → DimensionAxis::Staleness
    TimeOrdered,  // → DimensionAxis::Representation
    Monotonic,  // → DimensionAxis::Mutation
    AppendOnly,  // → DimensionAxis::Mutation
    HotPath,  // → DimensionAxis::Regime
    DetSafe,  // → DimensionAxis::Effect
    NumericalTier,  // → DimensionAxis::Precision
    Vendor,  // → DimensionAxis::Representation
    Hw,  // → DimensionAxis::HwInstruction
    BarrierGuarded,  // → DimensionAxis::BarrierStrength
    SimdWidthPinned,  // → DimensionAxis::SimdIsa
    ScopedFence,  // → DimensionAxis::MemoryScope
    ResidencyHeat,  // → DimensionAxis::Space
    CipherTier,  // → DimensionAxis::Security
    AllocClass,  // → DimensionAxis::Space
    Wait,  // → DimensionAxis::Synchronization
    MemOrder,  // → DimensionAxis::Synchronization
    Progress,  // → DimensionAxis::Complexity
    Consistency,  // → DimensionAxis::Version
    Witness,  // → DimensionAxis::Observability
    JoinPolicy,  // → DimensionAxis::Synchronization
    FpModePinned,  // → DimensionAxis::FpMode
    OpaqueLifetime,  // → DimensionAxis::Lifetime
    Crash,  // → DimensionAxis::Effect
    Budgeted,  // → DimensionAxis::Space
    EpochVersioned,  // → DimensionAxis::Version
    NumaPlacement,  // → DimensionAxis::Representation
    RecipeSpec,  // → DimensionAxis::Precision
};

inline constexpr std::size_t WRAPPER_KIND_COUNT = std::meta::enumerators_of(^^WrapperKind).size();

[[nodiscard]] constexpr DimensionAxis wrapper_kind_to_axis(WrapperKind k) noexcept {
    switch (k) {
        case WrapperKind::Linear:
            return DimensionAxis::Usage;
        case WrapperKind::Refined:
            return DimensionAxis::Refinement;
        case WrapperKind::SealedRefined:
            return DimensionAxis::Refinement;
        case WrapperKind::Tagged:
            return DimensionAxis::Provenance;
        case WrapperKind::Secret:
            return DimensionAxis::Security;
        case WrapperKind::Stale:
            return DimensionAxis::Staleness;
        case WrapperKind::TimeOrdered:
            return DimensionAxis::Representation;
        case WrapperKind::Monotonic:
            return DimensionAxis::Mutation;
        case WrapperKind::AppendOnly:
            return DimensionAxis::Mutation;
        case WrapperKind::HotPath:
            return DimensionAxis::Regime;
        case WrapperKind::DetSafe:
            return DimensionAxis::Effect;
        case WrapperKind::NumericalTier:
            return DimensionAxis::Precision;
        case WrapperKind::Vendor:
            return DimensionAxis::Representation;
        case WrapperKind::Hw:
            return DimensionAxis::HwInstruction;
        case WrapperKind::BarrierGuarded:
            return DimensionAxis::BarrierStrength;
        case WrapperKind::SimdWidthPinned:
            return DimensionAxis::SimdIsa;
        case WrapperKind::ScopedFence:
            return DimensionAxis::MemoryScope;
        case WrapperKind::ResidencyHeat:
            return DimensionAxis::Space;
        case WrapperKind::CipherTier:
            return DimensionAxis::Security;
        case WrapperKind::AllocClass:
            return DimensionAxis::Space;
        case WrapperKind::Wait:
            return DimensionAxis::Synchronization;
        case WrapperKind::MemOrder:
            return DimensionAxis::Synchronization;
        case WrapperKind::Progress:
            return DimensionAxis::Complexity;
        case WrapperKind::Consistency:
            return DimensionAxis::Version;
        case WrapperKind::Witness:
            return DimensionAxis::Observability;
        case WrapperKind::JoinPolicy:
            return DimensionAxis::Synchronization;
        case WrapperKind::FpModePinned:
            return DimensionAxis::FpMode;
        case WrapperKind::OpaqueLifetime:
            return DimensionAxis::Lifetime;
        case WrapperKind::Crash:
            return DimensionAxis::Effect;
        case WrapperKind::Budgeted:
            return DimensionAxis::Space;
        case WrapperKind::EpochVersioned:
            return DimensionAxis::Version;
        case WrapperKind::NumaPlacement:
            return DimensionAxis::Representation;
        case WrapperKind::RecipeSpec:
            return DimensionAxis::Precision;
        default:
            return DimensionAxis{0xFF};
    }
}

[[nodiscard]] constexpr std::string_view wrapper_kind_name(WrapperKind k) noexcept {
    switch (k) {
        case WrapperKind::Linear:
            return "Linear";
        case WrapperKind::Refined:
            return "Refined";
        case WrapperKind::SealedRefined:
            return "SealedRefined";
        case WrapperKind::Tagged:
            return "Tagged";
        case WrapperKind::Secret:
            return "Secret";
        case WrapperKind::Stale:
            return "Stale";
        case WrapperKind::TimeOrdered:
            return "TimeOrdered";
        case WrapperKind::Monotonic:
            return "Monotonic";
        case WrapperKind::AppendOnly:
            return "AppendOnly";
        case WrapperKind::HotPath:
            return "HotPath";
        case WrapperKind::DetSafe:
            return "DetSafe";
        case WrapperKind::NumericalTier:
            return "NumericalTier";
        case WrapperKind::Vendor:
            return "Vendor";
        case WrapperKind::Hw:
            return "Hw";
        case WrapperKind::BarrierGuarded:
            return "BarrierGuarded";
        case WrapperKind::SimdWidthPinned:
            return "SimdWidthPinned";
        case WrapperKind::ScopedFence:
            return "ScopedFence";
        case WrapperKind::ResidencyHeat:
            return "ResidencyHeat";
        case WrapperKind::CipherTier:
            return "CipherTier";
        case WrapperKind::AllocClass:
            return "AllocClass";
        case WrapperKind::Wait:
            return "Wait";
        case WrapperKind::MemOrder:
            return "MemOrder";
        case WrapperKind::Progress:
            return "Progress";
        case WrapperKind::Consistency:
            return "Consistency";
        case WrapperKind::Witness:
            return "Witness";
        case WrapperKind::JoinPolicy:
            return "JoinPolicy";
        case WrapperKind::FpModePinned:
            return "FpModePinned";
        case WrapperKind::OpaqueLifetime:
            return "OpaqueLifetime";
        case WrapperKind::Crash:
            return "Crash";
        case WrapperKind::Budgeted:
            return "Budgeted";
        case WrapperKind::EpochVersioned:
            return "EpochVersioned";
        case WrapperKind::NumaPlacement:
            return "NumaPlacement";
        case WrapperKind::RecipeSpec:
            return "RecipeSpec";
        default:
            return std::string_view{"<unknown WrapperKind>"};
    }
}

// Reflection cannot enumerate template specializations, so the count of
// wrapper_dimension specializations is pinned by hand here. Ordinals are
// append-only: a consumer that hashed one must keep getting the same value.
static_assert(WRAPPER_KIND_COUNT == 33, "WrapperKind enumerator count drifted from the 33 wrapper_dimension "
                                        "specializations declared at lines 566..746.  Adding a new wrapper "
                                        "requires APPEND-ONLY WrapperKind enumerator + wrapper_kind_to_axis "
                                        "arm + wrapper_kind_name arm.  Existing ordinals never change (the "
                                        "federation cache key for any consumer that hashed a WrapperKind "
                                        "ordinal never drifts across append-only growth).");

[[nodiscard]] consteval bool every_wrapper_kind_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^WrapperKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = wrapper_kind_name([:en:]);
        if (n == std::string_view{"<unknown WrapperKind>"}) return false;
        if (n.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_wrapper_kind_has_name(), "wrapper_kind_name() missing arm for at least one WrapperKind — "
                                             "add the arm or the new wrapper kind leaks the '<unknown "
                                             "WrapperKind>' sentinel.");

[[nodiscard]] consteval bool every_wrapper_kind_has_axis() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^WrapperKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto a = wrapper_kind_to_axis([:en:]);
        if (dimension_axis_name(a) == std::string_view{"<unknown DimensionAxis>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_wrapper_kind_has_axis(), "wrapper_kind_to_axis() switch missing arm for at least one "
                                             "WrapperKind — add the arm or new wrapper kinds silently fall "
                                             "through to the unreachable DimensionAxis{0xFF} sentinel.");

[[nodiscard]] consteval std::size_t count_wrappers_on_axis(DimensionAxis d) noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^WrapperKind));
    std::size_t n = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (wrapper_kind_to_axis([:en:]) == d) ++n;
    }
#pragma GCC diagnostic pop
    return n;
}

template <DimensionAxis D>
[[nodiscard]] consteval auto wrapper_for() noexcept -> std::array<WrapperKind, count_wrappers_on_axis(D)> {
    std::array<WrapperKind, count_wrappers_on_axis(D)> out{};
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^WrapperKind));
    std::size_t i = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (wrapper_kind_to_axis([:en:]) == D) {
            out[i++] = [:en:];
        }
    }
#pragma GCC diagnostic pop
    return out;
}

template <DimensionAxis D>
inline constexpr auto wrapper_for_v = wrapper_for<D>();

namespace detail::dimension_traits_self_test {

static_assert(TIER_KIND_COUNT == 5, "TierKind catalog diverged from fixy.md §24.1 Tier S/L/T/F/V (5); "
                                    "if intentional, update fixy.md and this constant together.");
static_assert(DIMENSION_AXIS_COUNT == 33, "DimensionAxis catalog diverged from fixy.md §24.1 (33 dims: FX's "
                                          "22 minus dim 12 Clock Domain and dim 17 FP Order, plus the Crucible "
                                          "Synchronization extension added 2026-05-18 for Wait + MemOrder, plus "
                                          "the Crucible Regime extension added 2026-05-18 for HotPath, plus "
                                          "the Crucible FpMode extension added 2026-05-22 for the 11-sub-axis "
                                          "FP-mode taxonomy per FIXY-V-088, plus the Crucible SyscallSurface "
                                          "extension added 2026-05-22 for the syscall-family taxonomy per "
                                          "FIXY-V-097, plus the five Crucible function-behavior extensions added "
                                          "2026-05-23 (ControlFlow / CallShape / StackUse / GlobalState / Stdio) "
                                          "per FIXY-V-238, plus the three Crucible hardware-instruction "
                                          "extensions added 2026-05-23 (HwInstruction / BarrierStrength / "
                                          "SimdIsa) per FIXY-V-253, plus the Crucible MemoryScope extension "
                                          "added 2026-05-23 for the memory-visibility-scope taxonomy per "
                                          "FIXY-V-266); if intentional, update fixy.md §24.1 + "
                                          "§24.14 + §24.15 + §24.16 + §24.17 + §24.18 + §24.19 + §24.20 and this "
                                          "constant.");

[[nodiscard]] consteval bool every_tier_kind_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^TierKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = tier_kind_name([:en:]);
        if (n == std::string_view{"<unknown TierKind>"}) return false;
        if (n.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_tier_kind_has_name(), "tier_kind_name() missing arm for at least one TierKind — add the "
                                          "arm or the new tier leaks the '<unknown TierKind>' sentinel.");

[[nodiscard]] consteval bool every_dimension_axis_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = dimension_axis_name([:en:]);
        if (n == std::string_view{"<unknown DimensionAxis>"}) return false;
        if (n.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_dimension_axis_has_name(), "dimension_axis_name() missing arm for at least one DimensionAxis — "
                                               "add the arm or the new axis leaks the '<unknown DimensionAxis>' "
                                               "sentinel.");

[[nodiscard]] consteval bool every_dimension_axis_has_tier() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto t = tier_of_axis([:en:]);
        if (tier_kind_name(t) == std::string_view{"<unknown TierKind>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_dimension_axis_has_tier(), "tier_of_axis() switch missing arm for at least one DimensionAxis — "
                                               "add the arm or new axes silently fall through to the unreachable "
                                               "TierKind{0xFF} sentinel.");

[[nodiscard]] consteval std::size_t count_dims_in_tier(TierKind t) noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^DimensionAxis));
    std::size_t n = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (tier_of_axis([:en:]) == t) ++n;
    }
#pragma GCC diagnostic pop
    return n;
}

static_assert(count_dims_in_tier(TierKind::Semiring) == 26,
              "fixy.md §24.1 declares 26 Tier-S dimensions (15 FX-inherited + "
              "Synchronization 2026-05-18 per fixy-A3-008 + Regime 2026-05-18 per "
              "fixy-A3-009 + FpMode 2026-05-22 per FIXY-V-088 + SyscallSurface "
              "2026-05-22 per FIXY-V-097 + ControlFlow / CallShape / StackUse / "
              "GlobalState / Stdio 2026-05-23 per FIXY-V-238 + HwInstruction / "
              "BarrierStrength 2026-05-23 per FIXY-V-253); tier_of_axis "
              "disagrees.");
static_assert(count_dims_in_tier(TierKind::Lattice) == 3,
              "fixy.md §24.1 declares 3 Tier-L dimensions (Representation + SimdIsa "
              "2026-05-23 per FIXY-V-253 + MemoryScope 2026-05-23 per FIXY-V-266); "
              "tier_of_axis disagrees.");
static_assert(count_dims_in_tier(TierKind::Typestate) == 1, "fixy.md §24.1 declares 1 Tier-T dimension (Protocol); "
                                                            "tier_of_axis disagrees.");
static_assert(count_dims_in_tier(TierKind::Foundational) == 2,
              "fixy.md §24.1 declares 2 Tier-F dimensions (Type, Refinement); "
              "tier_of_axis disagrees.");
static_assert(count_dims_in_tier(TierKind::Versioned) == 1, "fixy.md §24.1 declares 1 Tier-V dimension (Version); "
                                                            "tier_of_axis disagrees.");

static_assert(count_dims_in_tier(TierKind::Semiring) + count_dims_in_tier(TierKind::Lattice)
                      + count_dims_in_tier(TierKind::Typestate) + count_dims_in_tier(TierKind::Foundational)
                      + count_dims_in_tier(TierKind::Versioned)
                  == DIMENSION_AXIS_COUNT,
              "Sum of per-Tier dimension counts does not equal DIMENSION_AXIS_COUNT "
              "— a dimension is either uncounted or double-counted in tier_of_axis.");

struct TestLattice {
    using element_type = bool;
    [[nodiscard]] static constexpr bool bottom() noexcept { return false; }
    [[nodiscard]] static constexpr bool top() noexcept { return true; }
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
};

struct TestSemiring {
    using element_type = bool;
    [[nodiscard]] static constexpr bool bottom() noexcept { return false; }
    [[nodiscard]] static constexpr bool top() noexcept { return true; }
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
    [[nodiscard]] static constexpr bool zero() noexcept { return false; }
    [[nodiscard]] static constexpr bool one() noexcept { return true; }
    [[nodiscard]] static constexpr bool add(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool mul(bool a, bool b) noexcept { return a && b; }
};

struct TestVersioned {
    using element_type = std::uint32_t;
    [[nodiscard]] static constexpr bool compatible(std::uint32_t a, std::uint32_t b) noexcept { return a == b; }
};

struct TestTypestate {
    using state_type = int;
    using transition_type = int;
};

struct TestBareFoundational {
    int payload{0};
};

static_assert(LatticeGrade<TestLattice>);
static_assert(!SemiringGrade<TestLattice>);  // No add/mul.

static_assert(LatticeGrade<TestSemiring>);
static_assert(SemiringGrade<TestSemiring>);  // Both shapes.

static_assert(VersionedGrade<TestVersioned>);
static_assert(!LatticeGrade<TestVersioned>);
static_assert(!TypestateGrade<TestVersioned>);

static_assert(TypestateGrade<TestTypestate>);
static_assert(!LatticeGrade<TestTypestate>);
static_assert(!VersionedGrade<TestTypestate>);

static_assert(FoundationalGrade<int>);
static_assert(FoundationalGrade<TestBareFoundational>);

static_assert(tier_for_grade_v<TestSemiring> == TierKind::Semiring);
static_assert(tier_for_grade_v<TestLattice> == TierKind::Lattice);
static_assert(tier_for_grade_v<TestTypestate> == TierKind::Typestate);
static_assert(tier_for_grade_v<TestVersioned> == TierKind::Versioned);
static_assert(tier_for_grade_v<TestBareFoundational> == TierKind::Foundational);
static_assert(tier_for_grade_v<int> == TierKind::Foundational);

static_assert(tier_kind_name(TierKind::Semiring) == "Tier-S (Semiring)");
static_assert(tier_kind_name(TierKind::Lattice) == "Tier-L (Lattice)");
static_assert(tier_kind_name(TierKind::Typestate) == "Tier-T (Typestate)");
static_assert(tier_kind_name(TierKind::Foundational) == "Tier-F (Foundational)");
static_assert(tier_kind_name(TierKind::Versioned) == "Tier-V (Versioned)");

static_assert(dimension_axis_name(DimensionAxis::Type) == "Type");
static_assert(dimension_axis_name(DimensionAxis::Refinement) == "Refinement");
static_assert(dimension_axis_name(DimensionAxis::Usage) == "Usage");
static_assert(dimension_axis_name(DimensionAxis::Effect) == "Effect");
static_assert(dimension_axis_name(DimensionAxis::Security) == "Security");
static_assert(dimension_axis_name(DimensionAxis::Protocol) == "Protocol");
static_assert(dimension_axis_name(DimensionAxis::Lifetime) == "Lifetime");
static_assert(dimension_axis_name(DimensionAxis::Provenance) == "Provenance");
static_assert(dimension_axis_name(DimensionAxis::Trust) == "Trust");
static_assert(dimension_axis_name(DimensionAxis::Representation) == "Representation");
static_assert(dimension_axis_name(DimensionAxis::Observability) == "Observability");
static_assert(dimension_axis_name(DimensionAxis::Complexity) == "Complexity");
static_assert(dimension_axis_name(DimensionAxis::Precision) == "Precision");
static_assert(dimension_axis_name(DimensionAxis::Space) == "Space");
static_assert(dimension_axis_name(DimensionAxis::Overflow) == "Overflow");
static_assert(dimension_axis_name(DimensionAxis::Mutation) == "Mutation");
static_assert(dimension_axis_name(DimensionAxis::Reentrancy) == "Reentrancy");
static_assert(dimension_axis_name(DimensionAxis::Size) == "Size");
static_assert(dimension_axis_name(DimensionAxis::Version) == "Version");
static_assert(dimension_axis_name(DimensionAxis::Staleness) == "Staleness");
static_assert(dimension_axis_name(DimensionAxis::Synchronization) == "Synchronization");
static_assert(dimension_axis_name(DimensionAxis::Regime) == "Regime");
static_assert(dimension_axis_name(DimensionAxis::FpMode) == "FpMode");
static_assert(dimension_axis_name(DimensionAxis::SyscallSurface) == "SyscallSurface");
static_assert(dimension_axis_name(DimensionAxis::ControlFlow) == "ControlFlow");
static_assert(dimension_axis_name(DimensionAxis::CallShape) == "CallShape");
static_assert(dimension_axis_name(DimensionAxis::StackUse) == "StackUse");
static_assert(dimension_axis_name(DimensionAxis::GlobalState) == "GlobalState");
static_assert(dimension_axis_name(DimensionAxis::Stdio) == "Stdio");
static_assert(dimension_axis_name(DimensionAxis::HwInstruction) == "HwInstruction");
static_assert(dimension_axis_name(DimensionAxis::BarrierStrength) == "BarrierStrength");
static_assert(dimension_axis_name(DimensionAxis::SimdIsa) == "SimdIsa");
static_assert(dimension_axis_name(DimensionAxis::MemoryScope) == "MemoryScope");

static_assert(tier_of_axis(DimensionAxis::Type) == TierKind::Foundational);
static_assert(tier_of_axis(DimensionAxis::Refinement) == TierKind::Foundational);
static_assert(tier_of_axis(DimensionAxis::Usage) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Effect) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Protocol) == TierKind::Typestate);
static_assert(tier_of_axis(DimensionAxis::Representation) == TierKind::Lattice);
static_assert(tier_of_axis(DimensionAxis::Version) == TierKind::Versioned);
static_assert(tier_of_axis(DimensionAxis::Staleness) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Synchronization) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Regime) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::FpMode) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::SyscallSurface) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::ControlFlow) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::CallShape) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::StackUse) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::GlobalState) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Stdio) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::HwInstruction) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::BarrierStrength) == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::SimdIsa) == TierKind::Lattice);

static_assert(tier_of_axis_v<DimensionAxis::Type> == TierKind::Foundational);
static_assert(tier_of_axis_v<DimensionAxis::Effect> == TierKind::Semiring);
static_assert(tier_of_axis_v<DimensionAxis::Version> == TierKind::Versioned);

struct QuadTag {};
using WLinear = Linear<int>;
using WRefined = Refined<positive, int>;
using WSealedRefined = SealedRefined<positive, int>;
using WTagged = Tagged<int, source::FromUser>;
using WSecret = Secret<int>;
using WStale = Stale<int>;
using WTimeOrdered = TimeOrdered<int, 4, QuadTag>;
using WMonotonic = Monotonic<std::uint64_t>;
using WAppendOnly = AppendOnly<int>;
using WHotPath = HotPath<HotPathTier_v::Hot, int>;
using WDetSafe = DetSafe<DetSafeTier_v::Pure, int>;
using WNumericalTier = NumericalTier<Tolerance::BITEXACT, int>;
using WVendor = Vendor<VendorBackend_v::Portable, int>;
using WResidencyHeat = ResidencyHeat<ResidencyHeatTag_v::Hot, int>;
using WCipherTier = CipherTier<CipherTierTag_v::Hot, int>;
using WAllocClass = AllocClass<AllocClassTag_v::Arena, int>;
using WWait = Wait<WaitStrategy_v::SpinPause, int>;
using WMemOrder = MemOrder<MemOrderTag_v::SeqCst, int>;
using WProgress = Progress<ProgressClass_v::Bounded, int>;
using WConsistency = Consistency<Consistency_v::STRONG, int>;
using WOpaqueLifetime = OpaqueLifetime<Lifetime_v::PER_REQUEST, int>;
using WCrash = Crash<CrashClass_v::NoThrow, int>;
using WBudgeted = Budgeted<int>;
using WEpochVersioned = EpochVersioned<int>;
using WNumaPlacement = NumaPlacement<int>;
using WRecipeSpec = RecipeSpec<int>;
using WWitness = Witness<Witness_v::FORMALLY_VERIFIED, int>;
using WHw = Hw<HwInstruction_v::Scalar, int>;
using WBarrierGuarded = BarrierGuarded<BarrierStrength_v::AcqRel, int>;
using WSimdWidthPinned = SimdWidthPinned<SimdIsa_v::Scalar, int>;
using WScopedFence = ScopedFence<MemoryScope_v::Thread, int>;
using WJoinPolicy = JoinPolicy<JoinPolicy_v::DETACH, int>;
using WFpModePinned = FpModePinned<FpRounding::RoundToNearestEven, int>;

static_assert(wrapper_tier_v<WLinear> == TierKind::Semiring);
static_assert(wrapper_tier_v<WRefined> == TierKind::Foundational);
static_assert(wrapper_tier_v<WTagged> == TierKind::Semiring);
static_assert(wrapper_tier_v<WSecret> == TierKind::Semiring);
static_assert(wrapper_tier_v<WTimeOrdered> == TierKind::Lattice);
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
static_assert(verify_quadruple<WWitness>());
static_assert(verify_quadruple<WHw>());
static_assert(verify_quadruple<WBarrierGuarded>());
static_assert(verify_quadruple<WSimdWidthPinned>());
static_assert(verify_quadruple<WScopedFence>());
static_assert(verify_quadruple<WJoinPolicy>());
static_assert(verify_quadruple<WFpModePinned>());

// The pair below is the carrier-shape distinction inside the Tier-S
// population. Stale carries a full semiring, so the strict check admits it.
static_assert(tier_admits_semiring<wrapper_tier_v<WStale>, wrapper_lattice_t<WStale>>());

// HotPath pins a one-element carrier with no add or mul, so the strict check
// rejects it. The rejection is the point: it shows the strict variant really
// does separate the two carrier shapes.
static_assert(!tier_admits_semiring<wrapper_tier_v<WHotPath>, wrapper_lattice_t<WHotPath>>());

static_assert(wrapper_dimension_v<WWitness> == DimensionAxis::Observability);
static_assert(wrapper_tier_v<WWitness> == TierKind::Semiring);

}  // namespace detail::dimension_traits_self_test

}  // namespace crucible::safety
