#pragma once

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Capability.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/fixy/Default.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Hw.h>
#include <crucible/fixy/Profile.h>
#include <crucible/fixy/Reject.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/Tagged.h>

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::fixy {

namespace detail::resolve {

// A defined primary template names the offending tag in the instantiation
// context. An undefined one reports only an incomplete type, which does not
// distinguish a missing specialization from a misrouted axis.

template <typename G>
struct project {
    static_assert(::crucible::fixy::detail::diagnose::always_false_v<G>,
                  "fixy::fn<Type, Grants...>: project<G> reached for a grant tag "
                  "with no specialization.  The G template parameter on this "
                  "project<...> instantiation names the offending tag.  Two ways "
                  "to repair:\n"
                  "  (a) A per-domain grant tag routed to a substrate axis — if G "
                  "is final, derives grant_base and has a which_dim<G> "
                  "specialization on a non-Type DimensionAxis, specialize "
                  "::crucible::fixy::detail::resolve::project<G> alongside the tag "
                  "to expose ::type (type-valued axes: Refinement / Effect / "
                  "Protocol / Lifetime / Provenance / Trust / Complexity / "
                  "Precision / Space / Size / Staleness) or ::value + ::value_type "
                  "(enum-valued and integer-valued axes: Usage / Security / "
                  "Representation / Overflow / Mutation / Reentrancy / Version).\n"
                  "  (b) A non-grant type leaked through IsGrantTag — if G is not "
                  "a grant tag, make `fixy::grant::IsGrantTag_v<G>` false so the "
                  "type is rejected at the grant-validation tier and never reaches "
                  "project<G>.  FixyMalformedGrant is the correct catch-point for "
                  "non-grant inputs.");
};

template <dim::DimensionAxis D>
struct project<grant::accept_default_strict_for<D>> : strict_default_for<D> {};

template <typename Pred>
struct project<grant::refined_with<Pred>> {
    using type = Pred;
};

template <>
struct project<grant::affine> {
    using value_type = safety::fn::UsageMode;
    static constexpr value_type value = safety::fn::UsageMode::Affine;
};
template <>
struct project<grant::copy> {
    using value_type = safety::fn::UsageMode;
    static constexpr value_type value = safety::fn::UsageMode::Copy;
};
template <>
struct project<grant::ghost> {
    using value_type = safety::fn::UsageMode;
    static constexpr value_type value = safety::fn::UsageMode::Ghost;
};
template <>
struct project<grant::borrow> {
    using value_type = safety::fn::UsageMode;
    static constexpr value_type value = safety::fn::UsageMode::Borrow;
};
template <>
struct project<grant::capability_usage> {
    using value_type = safety::fn::UsageMode;
    static constexpr value_type value = safety::fn::UsageMode::Capability;
};

template <effects::Effect... Es>
struct project<grant::with<Es...>> {
    using type = effects::Row<Es...>;
};

// A declassification lands at the SecLevel its policy authorizes, which is
// not always Public. Specializing per Policy keeps the Security lattice
// stratified. A single fixed target would collapse every policy onto one
// point and hide the difference from the type system.

template <typename Policy>
struct declassify_target {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Public;
};

template <typename Policy>
inline constexpr safety::fn::SecLevel declassify_target_v = declassify_target<Policy>::value;

template <typename Policy>
struct project<grant::declassify<Policy>> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = declassify_target_v<Policy>;
};

namespace found_032_witness {

struct LatticeNonCollapseProofPolicy {};

}  // namespace found_032_witness

}  // namespace detail::resolve

template <>
struct detail::resolve::declassify_target<detail::resolve::found_032_witness::LatticeNonCollapseProofPolicy> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Classified;
};

namespace detail::resolve {

static_assert(declassify_target_v<int> == safety::fn::SecLevel::Public,
              "declassify_target<Policy> must fall through to SecLevel::Public "
              "for a Policy with no specialization of its own.");

static_assert(declassify_target_v<found_032_witness::LatticeNonCollapseProofPolicy> == safety::fn::SecLevel::Classified,
              "A per-Policy declassify_target<> specialization must override the "
              "default Public target.  Without the override every declassification "
              "lands at Public and the Security lattice loses its stratification.");

template <>
struct project<grant::as_unclassified> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Unclassified;
};
template <>
struct project<grant::as_public> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Public;
};
template <>
struct project<grant::as_internal> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Internal;
};
template <>
struct project<grant::as_classified> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Classified;
};
template <>
struct project<grant::as_secret> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Secret;
};

template <typename Proto>
struct project<grant::protocol<Proto>> {
    using type = Proto;
};

template <auto RegionTag>
struct project<grant::in_region<RegionTag>> {
    using type = safety::fn::lifetime::In<RegionTag>;
};

template <typename Source>
struct project<grant::from_source<Source>> {
    using type = Source;
};

template <auto Rationale>
struct project<grant::trust_assumed<Rationale>> {
    using type = safety::trust::Assumed;
};

template <>
struct project<grant::trust_verified> {
    using type = safety::trust::Verified;
};
template <>
struct project<grant::trust_tested> {
    using type = safety::trust::Tested;
};
template <>
struct project<grant::trust_unverified> {
    using type = safety::trust::Unverified;
};
template <>
struct project<grant::trust_external> {
    using type = safety::trust::External;
};

template <safety::fn::ReprKind Kind>
struct project<grant::repr<Kind>> {
    using value_type = safety::fn::ReprKind;
    static constexpr value_type value = Kind;
};

template <>
struct project<grant::cost_constant> {
    using type = safety::fn::cost::Constant;
};
template <auto N>
struct project<grant::cost_linear<N>> {
    using type = safety::fn::cost::Linear<N>;
};
template <auto N>
struct project<grant::cost_quadratic<N>> {
    using type = safety::fn::cost::Quadratic<N>;
};
template <>
struct project<grant::cost_unbounded> {
    using type = safety::fn::cost::Unbounded;
};

template <>
struct project<grant::precision_f32> {
    using type = safety::fn::precision::F32;
};
template <>
struct project<grant::precision_f64> {
    using type = safety::fn::precision::F64;
};
template <auto Bound>
struct project<grant::precision_higham<Bound>> {
    using type = safety::fn::precision::Higham<Bound>;
};

template <auto N>
struct project<grant::space_bounded<N>> {
    using type = safety::fn::space::Bounded<N>;
};
template <>
struct project<grant::space_unbounded> {
    using type = safety::fn::space::Unbounded;
};

template <>
struct project<grant::overflow_wrap> {
    using value_type = safety::fn::OverflowMode;
    static constexpr value_type value = safety::fn::OverflowMode::Wrap;
};
template <>
struct project<grant::overflow_saturate> {
    using value_type = safety::fn::OverflowMode;
    static constexpr value_type value = safety::fn::OverflowMode::Saturate;
};
template <>
struct project<grant::overflow_widen> {
    using value_type = safety::fn::OverflowMode;
    static constexpr value_type value = safety::fn::OverflowMode::Widen;
};

template <>
struct project<grant::mut_mutable> {
    using value_type = safety::fn::MutationMode;
    static constexpr value_type value = safety::fn::MutationMode::Mutable;
};
template <>
struct project<grant::mut_append> {
    using value_type = safety::fn::MutationMode;
    static constexpr value_type value = safety::fn::MutationMode::Append;
};
template <>
struct project<grant::mut_monotonic> {
    using value_type = safety::fn::MutationMode;
    static constexpr value_type value = safety::fn::MutationMode::Monotonic;
};

template <>
struct project<grant::reentrant> {
    using value_type = safety::fn::ReentrancyMode;
    static constexpr value_type value = safety::fn::ReentrancyMode::Reentrant;
};
template <>
struct project<grant::coroutine> {
    using value_type = safety::fn::ReentrancyMode;
    static constexpr value_type value = safety::fn::ReentrancyMode::Coroutine;
};

template <auto Depth>
struct project<grant::sized_at<Depth>> {
    using type = safety::fn::size_pol::Sized<Depth>;
};
template <>
struct project<grant::productive> {
    using type = safety::fn::size_pol::Productive;
};

template <std::uint32_t V>
struct project<grant::version<V>> {
    using value_type = std::uint32_t;
    static constexpr value_type value = V;
};

template <auto TauMax>
struct project<grant::stale_to<TauMax>> {
    using type = safety::fn::stale::Stale<TauMax>;
};

// An axis listed here has no grant tag of its own.  A binding on such an axis
// reaches `project<accept_default_strict_for<D>>` and can only accept the
// strict default.  No alternative stance is expressible.

namespace audit {

inline constexpr std::array<dim::DimensionAxis, 13> kAxesWithoutNonDefaultGrants = {
    dim::DimensionAxis::Synchronization, dim::DimensionAxis::Regime,          dim::DimensionAxis::FpMode,
    dim::DimensionAxis::SyscallSurface,  dim::DimensionAxis::ControlFlow,     dim::DimensionAxis::CallShape,
    dim::DimensionAxis::StackUse,        dim::DimensionAxis::GlobalState,     dim::DimensionAxis::Stdio,
    dim::DimensionAxis::HwInstruction,   dim::DimensionAxis::BarrierStrength, dim::DimensionAxis::SimdIsa,
    dim::DimensionAxis::MemoryScope,
};

static_assert(kAxesWithoutNonDefaultGrants.size() == 13,
              "Cardinality pin on the list of axes without a grant family.  When "
              "the first non-default grant ships for a listed axis, drop that axis "
              "from `kAxesWithoutNonDefaultGrants` and decrement this assertion.  "
              "When a new axis lands with no grant family, append it and increment "
              "this assertion.  A fire means the list and the count disagree.");

[[nodiscard]] constexpr bool axis_has_grant_family(dim::DimensionAxis D) noexcept {
    for (auto gap_axis : kAxesWithoutNonDefaultGrants) {
        if (gap_axis == D) return false;
    }
    return true;
}

// The Observability axis has no grant tag either, but it is derived from the
// Effect axis rather than awaiting a grant family, so it stays out of the list
// above and counts on this side of the pin.
static_assert(safety::DIMENSION_AXIS_COUNT - kAxesWithoutNonDefaultGrants.size() == 20,
              "20 axes either ship a grant family or are structurally derived "
              "(Observability).  A fire means a DimensionAxis was added or "
              "removed without updating the list of axes that lack a grant "
              "family.");

}  // namespace audit

// The empty-pack base case is unreachable once IsAccepted has passed, since
// an accepted pack engages every axis.  It returns the acceptance marker so
// that `project` still has a specialization to consult when a caller bypasses
// the gate.

template <dim::DimensionAxis D, typename... Grants>
struct find_grant_impl;

template <dim::DimensionAxis D>
struct find_grant_impl<D> {
    using type = grant::accept_default_strict_for<D>;
};

// Two partial specializations rather than one `std::conditional_t`: the
// conditional instantiates both branches, so `which_dim_v<G>` would be
// substituted for a non-grant G and fail hard inside the resolver instead of
// being rejected cleanly at the acceptance gate.  Constraint partial ordering
// picks the constrained form for a grant on axis D, and the unconstrained
// recursion never touches `which_dim_v<G>`.
template <dim::DimensionAxis D, typename G, typename... Rest>
struct find_grant_impl<D, G, Rest...> {
    using type = typename find_grant_impl<D, Rest...>::type;
};

template <dim::DimensionAxis D, typename G, typename... Rest>
    requires(grant::IsGrantTag_v<G> && grant::which_dim_v<G> == D)
struct find_grant_impl<D, G, Rest...> {
    using type = G;
};

template <dim::DimensionAxis D, typename... Grants>
using find_grant_t = typename find_grant_impl<D, Grants...>::type;

// The Security slot of the resolved binding carries only the resulting
// SecLevel, so the Policy that authorized a declassification would otherwise
// be unrecoverable.  This scan keeps it visible to audit code.  No
// IsGrantTag_v gate is needed because `grant::declassify<P>` is the only
// shape the middle specialization can match.
template <typename... Grants>
struct find_declassify_policy {
    using type = void;
};
template <typename Policy, typename... Rest>
struct find_declassify_policy<grant::declassify<Policy>, Rest...> {
    using type = Policy;
};
template <typename G, typename... Rest>
struct find_declassify_policy<G, Rest...> : find_declassify_policy<Rest...> {};

template <typename... Grants>
using find_declassify_policy_t = typename find_declassify_policy<Grants...>::type;

template <typename... Grants>
using resolve_refinement_t = typename project<find_grant_t<dim::DimensionAxis::Refinement, Grants...>>::type;
template <typename... Grants>
using resolve_effect_t = typename project<find_grant_t<dim::DimensionAxis::Effect, Grants...>>::type;
template <typename... Grants>
using resolve_protocol_t = typename project<find_grant_t<dim::DimensionAxis::Protocol, Grants...>>::type;
template <typename... Grants>
using resolve_lifetime_t = typename project<find_grant_t<dim::DimensionAxis::Lifetime, Grants...>>::type;
template <typename... Grants>
using resolve_source_t = typename project<find_grant_t<dim::DimensionAxis::Provenance, Grants...>>::type;
template <typename... Grants>
using resolve_trust_t = typename project<find_grant_t<dim::DimensionAxis::Trust, Grants...>>::type;
template <typename... Grants>
using resolve_cost_t = typename project<find_grant_t<dim::DimensionAxis::Complexity, Grants...>>::type;
template <typename... Grants>
using resolve_precision_t = typename project<find_grant_t<dim::DimensionAxis::Precision, Grants...>>::type;
template <typename... Grants>
using resolve_space_t = typename project<find_grant_t<dim::DimensionAxis::Space, Grants...>>::type;
template <typename... Grants>
using resolve_size_t = typename project<find_grant_t<dim::DimensionAxis::Size, Grants...>>::type;
template <typename... Grants>
using resolve_staleness_t = typename project<find_grant_t<dim::DimensionAxis::Staleness, Grants...>>::type;

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
inline constexpr std::uint32_t resolve_version_v = project<find_grant_t<dim::DimensionAxis::Version, Grants...>>::value;

template <typename Type, typename... Grants>
using resolved_fn_t =
    safety::fn::Fn<Type, resolve_refinement_t<Grants...>, resolve_usage_v<Grants...>, resolve_effect_t<Grants...>,
                   resolve_security_v<Grants...>, resolve_protocol_t<Grants...>, resolve_lifetime_t<Grants...>,
                   resolve_source_t<Grants...>, resolve_trust_t<Grants...>, resolve_repr_v<Grants...>,
                   resolve_cost_t<Grants...>, resolve_precision_t<Grants...>, resolve_space_t<Grants...>,
                   resolve_overflow_v<Grants...>, resolve_mutation_v<Grants...>, resolve_reentrancy_v<Grants...>,
                   resolve_size_t<Grants...>, resolve_version_v<Grants...>, resolve_staleness_t<Grants...>>;

// The caller never writes the Type-axis engagement marker.  The wrapper
// supplies it, so the caller's pack covers only the other axes.
using ImplicitTypeMarker = grant::accept_default_strict_for<dim::DimensionAxis::Type>;

// The acceptance concept keeps its own copy of this marker so that it does not
// have to depend on this header.  The two must stay the same type: a divergence
// would have the acceptance gate and the projection helpers inject different
// markers on paths that are meant to agree.
static_assert(std::is_same_v<ImplicitTypeMarker, ::crucible::fixy::detail::accept::ImplicitTypeMarker>,
              "The Type-axis injection marker used by the projection helpers must "
              "be the same type as the one the acceptance concept injects.  If the "
              "two diverge, the acceptance gate and the resolver disagree about "
              "which marker a binding carries.");

}  // namespace detail::resolve

namespace detail {

template <typename T>
concept TypeIsStanceCompatible = !std::is_void_v<T> && !std::is_array_v<T> && !std::is_reference_v<T>
                              && !std::is_const_v<T> && !std::is_volatile_v<T> && !std::is_function_v<T>;

}  // namespace detail

template <template <typename> class Stance, typename Type>
concept StanceForUnary = detail::TypeIsStanceCompatible<Type>;

// Policy takes the same shape constraint as Type because a declassification
// policy is a phantom tag whose identity is its bare class type.  Checking it
// here keeps the rejection at the function signature instead of deep inside
// the stance instantiation.
template <template <typename, typename> class Stance, typename Type, typename Policy>
concept StanceForBinary = detail::TypeIsStanceCompatible<Type> && detail::TypeIsStanceCompatible<Policy>;

// The sentinel's class name is itself part of the diagnostic: it appears
// verbatim in the compiler's "required from" trail, ahead of any message.

namespace detail::ctad {
struct fn_ctad_blocked_use_mint_fn_or_mint_fn_for final {};
}  // namespace detail::ctad

template <typename Type, typename... Grants>
class fn {
    using ImplicitTypeMarker = detail::resolve::ImplicitTypeMarker;

    // Declared before the diagnostic surface and the tier chain: the compiler
    // processes the class body in order, so this message reaches the user first
    // and the later tiers silence themselves against it.
    static constexpr bool fixy_a4_025_tier0_not_ctad_sentinel =
        !std::is_same_v<Type, detail::ctad::fn_ctad_blocked_use_mint_fn_or_mint_fn_for>;
    static_assert(fixy_a4_025_tier0_not_ctad_sentinel,
                  "fixy::fn<Type, Grants...> [tier 0: universal mint pattern]: "
                  "class template argument deduction (`fixy::fn{value}` / "
                  "`fixy::fn(value)`) is not supported.  Every value-carrying "
                  "fixy::fn is born through a mint factory, so that one grep for "
                  "\"mint_\" finds every binding.  Use one of:\n"
                  "  fixy::mint_fn<Type, Grants...>(value)         — explicit grants\n"
                  "  fixy::mint_fn_for<UnaryStance>(value)         — unary stance\n"
                  "  fixy::mint_fn_for<BinaryStance, Policy>(value) — binary stance\n"
                  "The fixy::stance namespace holds the canonical stance catalog.");

    // These three bases put the offending diagnostic tag's class name into the
    // compiler's "required from" trail.  They come before the tier chain
    // because the first failing tier assertion halts class-body processing and
    // would otherwise suppress them.  A `void` tag selects the empty Diagnose
    // specialization and stays silent, so the sentinel case shows the tier-0
    // message alone rather than a cascade.
    using fixy_h03_tier2_diag_tag = std::conditional_t<fixy_a4_025_tier0_not_ctad_sentinel,
                                                       malformed_grant_or_void_t<ImplicitTypeMarker, Grants...>, void>;
    using fixy_h03_tier3_diag_tag = std::conditional_t<fixy_a4_025_tier0_not_ctad_sentinel,
                                                       missing_tag_or_void_t<ImplicitTypeMarker, Grants...>, void>;
    using fixy_h03_tier4_diag_tag = std::conditional_t<fixy_a4_025_tier0_not_ctad_sentinel,
                                                       duplicate_tag_or_void_t<ImplicitTypeMarker, Grants...>, void>;

    struct fixy_h03_tier2_diagnose : DiagnoseMalformedGrant<fixy_h03_tier2_diag_tag> {};
    struct fixy_h03_tier3_diagnose : DiagnoseAxisNotEngaged<fixy_h03_tier3_diag_tag> {};
    struct fixy_h03_tier4_diagnose : DiagnoseAxisDuplicate<fixy_h03_tier4_diag_tag> {};

    // [temp.inst]/9: a member class of a class template is not implicitly
    // instantiated with its enclosing template.  Taking sizeof forces the
    // instantiation, which is what fires the Diagnose assertion.

    static_assert(sizeof(fixy_h03_tier2_diagnose) >= 1, "The malformed-grant diagnostic base must instantiate.");
    static_assert(sizeof(fixy_h03_tier3_diagnose) >= 1, "The unengaged-axis diagnostic base must instantiate.");
    static_assert(sizeof(fixy_h03_tier4_diagnose) >= 1, "The duplicate-axis diagnostic base must instantiate.");

    // The sentinel is an ordinary object type, so tier 1 would pass for it and
    // tier 3 would then fire on the empty pack.  Guarding every tier against
    // tier 0 keeps the sentinel case to one message.
    static constexpr bool fixy_h02_tier1_type_ok =
        !fixy_a4_025_tier0_not_ctad_sentinel || detail::accept::type_is_accepted_payload<Type>();
    static_assert(fixy_h02_tier1_type_ok, "fixy::fn<Type, Grants...> [tier 1: IsAccepted gate]: Type must be "
                                          "a non-cv, non-array, non-reference, non-function, non-void "
                                          "object type.  Wrap a bare function type as a pointer or a "
                                          "callable before instantiating fixy::fn.");

    static constexpr bool fixy_h02_tier2_grants_well_formed = !fixy_a4_025_tier0_not_ctad_sentinel
                                                           || !fixy_h02_tier1_type_ok
                                                           || AllGrantsWellFormed<ImplicitTypeMarker, Grants...>;
    // The message helper takes the caller's pack without the implicit marker,
    // so the position and count it reports match what the caller wrote.  The
    // check itself is unaffected, since the marker is always well formed.
    // Tier 4 keeps the marker because duplicate detection has to see the
    // marker's Type engagement to catch a caller who engages Type again.
    static_assert(fixy_h02_tier2_grants_well_formed, tier2_malformed_grant_message_v<Grants...>);

    // Sketch mode relaxes the engagement check and the corpus check only, for
    // in-progress migrations.  Type validity, grant well-formedness and unique
    // engagement stay strict in both modes, so sketch mode never admits a
    // binding the collision rules reject.
    static constexpr bool fixy_h02_tier3_all_dims_engaged =
        !fixy_a4_025_tier0_not_ctad_sentinel || !fixy_h02_tier1_type_ok || !fixy_h02_tier2_grants_well_formed
        || AllDimsEngaged<ImplicitTypeMarker, Grants...> || !fixy_is_strict;
    static_assert(fixy_h02_tier3_all_dims_engaged, tier3_missing_tag_message_v<ImplicitTypeMarker, Grants...>);

    static constexpr bool fixy_h02_tier4_unique_engagement =
        !fixy_a4_025_tier0_not_ctad_sentinel || !fixy_h02_tier1_type_ok || !fixy_h02_tier2_grants_well_formed
        || !fixy_h02_tier3_all_dims_engaged || UniqueEngagementPerAxis<ImplicitTypeMarker, Grants...>;
    static_assert(fixy_h02_tier4_unique_engagement, tier4_duplicate_tag_message_v<ImplicitTypeMarker, Grants...>);

    static constexpr bool fixy_h02_tier5_not_in_corpus =
        !fixy_a4_025_tier0_not_ctad_sentinel || !fixy_h02_tier1_type_ok || !fixy_h02_tier2_grants_well_formed
        || !fixy_h02_tier3_all_dims_engaged || !fixy_h02_tier4_unique_engagement
        || theory::NotInTheoryCorpus<Type, ImplicitTypeMarker, Grants...> || !fixy_is_strict;
    static_assert(fixy_h02_tier5_not_in_corpus, theory::corpus_full_diagnostic_v<Type, ImplicitTypeMarker, Grants...>);

public:
    using value_type = Type;
    using safety_fn_t = detail::resolve::resolved_fn_t<Type, Grants...>;

    using policy_t = detail::resolve::find_declassify_policy_t<Grants...>;

    using refinement_t = typename safety_fn_t::refinement_t;
    using effect_row_t = typename safety_fn_t::effect_row_t;
    using protocol_t = typename safety_fn_t::protocol_t;
    using lifetime_t = typename safety_fn_t::lifetime_t;
    using source_t = typename safety_fn_t::source_t;
    using trust_t = typename safety_fn_t::trust_t;
    using cost_t = typename safety_fn_t::cost_t;
    using precision_t = typename safety_fn_t::precision_t;
    using space_t = typename safety_fn_t::space_t;
    using size_t_ = typename safety_fn_t::size_t_;
    using staleness_t = typename safety_fn_t::staleness_t;

    static constexpr safety::fn::UsageMode usage_v = safety_fn_t::usage_v;
    static constexpr safety::fn::SecLevel security_v = safety_fn_t::security_v;
    static constexpr safety::fn::ReprKind repr_v = safety_fn_t::repr_v;
    static constexpr safety::fn::OverflowMode overflow_v = safety_fn_t::overflow_v;
    static constexpr safety::fn::MutationMode mutation_v = safety_fn_t::mutation_v;
    static constexpr safety::fn::ReentrancyMode reentrancy_v = safety_fn_t::reentrancy_v;
    static constexpr std::uint32_t version_v = safety_fn_t::version_v;

    // The wrapper does not override Type's copy and move semantics, even at a
    // Linear usage grade.  The grade records how the binding is meant to be
    // consumed, not how its storage behaves, so a fixy::fn<int> stays copyable
    // because int is.  Code that wants the runtime guarantee wraps a move-only
    // payload, and the defaulted copy then disappears on its own.  Deriving the
    // wrapper's copy semantics from the grade instead would tie one axis of the
    // discipline to the structural shape of the value.
    //
    // The default constructor stays public: it carries no authority, and the
    // size witnesses below need it.
    constexpr fn() = default;

    template <typename Self>
    [[nodiscard]] constexpr auto&& value(this Self&& self) noexcept {
        return std::forward<Self>(self).value_;
    }

private:
    // Wrapping a value is an authorization event: the caller asserts that this
    // value belongs under this grants pack.  Keeping the value constructor
    // private and befriending only the mint factories leaves one name to grep
    // for to find every such event.  A public value constructor would spread
    // the same authority across every `fn<...>{}` and `stance::...<...>{}`
    // spelling.
    explicit constexpr fn(Type v) noexcept(std::is_nothrow_move_constructible_v<Type>) : value_{std::move(v)} {}

    template <typename T, typename... G>
        requires IsAcceptedActive<T, G...>
    friend constexpr auto mint_fn(T) noexcept(std::is_nothrow_move_constructible_v<T>) -> fn<T, G...>;

    template <template <typename> class S, typename T>
        requires StanceForUnary<S, T>
    friend constexpr auto mint_fn_for(T) noexcept(std::is_nothrow_move_constructible_v<T>) -> S<T>;

    template <template <typename, typename> class S, typename P, typename T>
        requires StanceForBinary<S, T, P>
    friend constexpr auto mint_fn_for(T) noexcept(std::is_nothrow_move_constructible_v<T>) -> S<T, P>;

    Type value_{};
};

// The guide routes every deduction to the blocked sentinel on purpose.  With
// no guide at all, `fixy::fn{42}` stops at "no viable deduction guide", which
// names no remedy.  Deducing to the sentinel instead reaches the tier-0
// assertion, which names the mint factories.
template <typename T>
fn(T) -> fn<detail::ctad::fn_ctad_blocked_use_mint_fn_or_mint_fn_for>;

// The requires-clause repeats the check the class body already asserts.  It
// earns its place by putting the gate in the function signature, where a
// reader of the declaration sees it.

template <typename Type, typename... Grants>
    requires IsAcceptedActive<Type, Grants...>
[[nodiscard]] constexpr auto mint_fn(Type v) noexcept(std::is_nothrow_move_constructible_v<Type>)
    -> fn<Type, Grants...> {
    return fn<Type, Grants...>{std::move(v)};
}

// The concept gate rejects an ill-shaped Type at the signature.  Without it
// the same call fails inside the stance instantiation, where the diagnostic no
// longer points at the caller.
template <template <typename> class Stance, typename Type>
    requires StanceForUnary<Stance, Type>
[[nodiscard]] constexpr auto mint_fn_for(Type v) noexcept(std::is_nothrow_move_constructible_v<Type>) -> Stance<Type> {
    return Stance<Type>{std::move(v)};
}

// Policy precedes Type in the template parameter list.  Policy has no runtime
// carrier and must be written explicitly, so it has to come before the
// parameter that deduction fills in.
template <template <typename, typename> class Stance, typename Policy, typename Type>
    requires StanceForBinary<Stance, Type, Policy>
[[nodiscard]] constexpr auto mint_fn_for(Type v) noexcept(std::is_nothrow_move_constructible_v<Type>)
    -> Stance<Type, Policy> {
    return Stance<Type, Policy>{std::move(v)};
}

namespace stance {

namespace detail_stance {
template <dim::DimensionAxis D>
using strict = grant::accept_default_strict_for<D>;
}  // namespace detail_stance

template <typename Type>
using PureLinear = ::crucible::fixy::fn<
    Type, detail_stance::strict<dim::DimensionAxis::Refinement>, detail_stance::strict<dim::DimensionAxis::Usage>,
    detail_stance::strict<dim::DimensionAxis::Effect>, detail_stance::strict<dim::DimensionAxis::Security>,
    detail_stance::strict<dim::DimensionAxis::Protocol>, detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>, detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>, detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>, detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>, detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>, detail_stance::strict<dim::DimensionAxis::Reentrancy>,
    detail_stance::strict<dim::DimensionAxis::Size>, detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>, detail_stance::strict<dim::DimensionAxis::Synchronization>,
    detail_stance::strict<dim::DimensionAxis::Regime>, detail_stance::strict<dim::DimensionAxis::FpMode>,
    detail_stance::strict<dim::DimensionAxis::SyscallSurface>, detail_stance::strict<dim::DimensionAxis::ControlFlow>,
    detail_stance::strict<dim::DimensionAxis::CallShape>, detail_stance::strict<dim::DimensionAxis::StackUse>,
    detail_stance::strict<dim::DimensionAxis::GlobalState>, detail_stance::strict<dim::DimensionAxis::Stdio>,
    detail_stance::strict<dim::DimensionAxis::HwInstruction>,
    detail_stance::strict<dim::DimensionAxis::BarrierStrength>, detail_stance::strict<dim::DimensionAxis::SimdIsa>,
    detail_stance::strict<dim::DimensionAxis::MemoryScope>>;

template <typename Type>
using PureCopy = ::crucible::fixy::fn<
    Type, detail_stance::strict<dim::DimensionAxis::Refinement>, grant::copy,
    detail_stance::strict<dim::DimensionAxis::Effect>, detail_stance::strict<dim::DimensionAxis::Security>,
    detail_stance::strict<dim::DimensionAxis::Protocol>, detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>, detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>, detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>, detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>, detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>, detail_stance::strict<dim::DimensionAxis::Reentrancy>,
    detail_stance::strict<dim::DimensionAxis::Size>, detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>, detail_stance::strict<dim::DimensionAxis::Synchronization>,
    detail_stance::strict<dim::DimensionAxis::Regime>, detail_stance::strict<dim::DimensionAxis::FpMode>,
    detail_stance::strict<dim::DimensionAxis::SyscallSurface>, detail_stance::strict<dim::DimensionAxis::ControlFlow>,
    detail_stance::strict<dim::DimensionAxis::CallShape>, detail_stance::strict<dim::DimensionAxis::StackUse>,
    detail_stance::strict<dim::DimensionAxis::GlobalState>, detail_stance::strict<dim::DimensionAxis::Stdio>,
    detail_stance::strict<dim::DimensionAxis::HwInstruction>,
    detail_stance::strict<dim::DimensionAxis::BarrierStrength>, detail_stance::strict<dim::DimensionAxis::SimdIsa>,
    detail_stance::strict<dim::DimensionAxis::MemoryScope>>;

// Security is pinned here rather than left strict.  The strict Security
// default is Classified, and an IO channel is observable, so a classified
// payload leaks through it.  A payload that is classified but authorized for
// emission uses PublicEmit, which discharges the same rule through a policy.
template <typename Type>
using IoFunction = ::crucible::fixy::fn<
    Type, detail_stance::strict<dim::DimensionAxis::Refinement>, detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with_io, grant::as_public, detail_stance::strict<dim::DimensionAxis::Protocol>,
    detail_stance::strict<dim::DimensionAxis::Lifetime>, detail_stance::strict<dim::DimensionAxis::Provenance>,
    detail_stance::strict<dim::DimensionAxis::Trust>, detail_stance::strict<dim::DimensionAxis::Representation>,
    detail_stance::strict<dim::DimensionAxis::Observability>, detail_stance::strict<dim::DimensionAxis::Complexity>,
    detail_stance::strict<dim::DimensionAxis::Precision>, detail_stance::strict<dim::DimensionAxis::Space>,
    detail_stance::strict<dim::DimensionAxis::Overflow>, detail_stance::strict<dim::DimensionAxis::Mutation>,
    detail_stance::strict<dim::DimensionAxis::Reentrancy>, detail_stance::strict<dim::DimensionAxis::Size>,
    detail_stance::strict<dim::DimensionAxis::Version>, detail_stance::strict<dim::DimensionAxis::Staleness>,
    detail_stance::strict<dim::DimensionAxis::Synchronization>, detail_stance::strict<dim::DimensionAxis::Regime>,
    detail_stance::strict<dim::DimensionAxis::FpMode>, detail_stance::strict<dim::DimensionAxis::SyscallSurface>,
    detail_stance::strict<dim::DimensionAxis::ControlFlow>, detail_stance::strict<dim::DimensionAxis::CallShape>,
    detail_stance::strict<dim::DimensionAxis::StackUse>, detail_stance::strict<dim::DimensionAxis::GlobalState>,
    detail_stance::strict<dim::DimensionAxis::Stdio>, detail_stance::strict<dim::DimensionAxis::HwInstruction>,
    detail_stance::strict<dim::DimensionAxis::BarrierStrength>, detail_stance::strict<dim::DimensionAxis::SimdIsa>,
    detail_stance::strict<dim::DimensionAxis::MemoryScope>>;

// Security is pinned here rather than left strict, for the same reason as an
// IO binding: a spawn is scheduler-observable, so a spawn that depends on a
// classified value leaks it through the interleaving.  A worker over a
// classified payload declassifies explicitly instead of using this stance.
template <typename Type>
using BgWorker = ::crucible::fixy::fn<
    Type, detail_stance::strict<dim::DimensionAxis::Refinement>, detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with<effects::Effect::Bg, effects::Effect::Alloc>, grant::as_public,
    detail_stance::strict<dim::DimensionAxis::Protocol>, detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>, detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>, detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>, detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>, detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>, detail_stance::strict<dim::DimensionAxis::Reentrancy>,
    detail_stance::strict<dim::DimensionAxis::Size>, detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>, detail_stance::strict<dim::DimensionAxis::Synchronization>,
    detail_stance::strict<dim::DimensionAxis::Regime>, detail_stance::strict<dim::DimensionAxis::FpMode>,
    detail_stance::strict<dim::DimensionAxis::SyscallSurface>, detail_stance::strict<dim::DimensionAxis::ControlFlow>,
    detail_stance::strict<dim::DimensionAxis::CallShape>, detail_stance::strict<dim::DimensionAxis::StackUse>,
    detail_stance::strict<dim::DimensionAxis::GlobalState>, detail_stance::strict<dim::DimensionAxis::Stdio>,
    detail_stance::strict<dim::DimensionAxis::HwInstruction>,
    detail_stance::strict<dim::DimensionAxis::BarrierStrength>, detail_stance::strict<dim::DimensionAxis::SimdIsa>,
    detail_stance::strict<dim::DimensionAxis::MemoryScope>>;

template <typename Type, typename Policy>
using SecretConsumer = ::crucible::fixy::fn<
    Type, detail_stance::strict<dim::DimensionAxis::Refinement>, detail_stance::strict<dim::DimensionAxis::Usage>,
    detail_stance::strict<dim::DimensionAxis::Effect>, grant::declassify<Policy>,
    detail_stance::strict<dim::DimensionAxis::Protocol>, detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>, detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>, detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>, detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>, detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>, detail_stance::strict<dim::DimensionAxis::Reentrancy>,
    detail_stance::strict<dim::DimensionAxis::Size>, detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>, detail_stance::strict<dim::DimensionAxis::Synchronization>,
    detail_stance::strict<dim::DimensionAxis::Regime>, detail_stance::strict<dim::DimensionAxis::FpMode>,
    detail_stance::strict<dim::DimensionAxis::SyscallSurface>, detail_stance::strict<dim::DimensionAxis::ControlFlow>,
    detail_stance::strict<dim::DimensionAxis::CallShape>, detail_stance::strict<dim::DimensionAxis::StackUse>,
    detail_stance::strict<dim::DimensionAxis::GlobalState>, detail_stance::strict<dim::DimensionAxis::Stdio>,
    detail_stance::strict<dim::DimensionAxis::HwInstruction>,
    detail_stance::strict<dim::DimensionAxis::BarrierStrength>, detail_stance::strict<dim::DimensionAxis::SimdIsa>,
    detail_stance::strict<dim::DimensionAxis::MemoryScope>>;

// A constant-time path does no IO at all, because any IO trip is timing-
// observable and reopens the side channel the discipline closes.  The strict
// Effect default is already the empty row, but the empty row is spelled out
// here so that a later widening of that default cannot relax this stance.  The
// strict defaults carry the rest: Linear usage, since duplicating a secret
// defeats the discipline, and non-reentrant, since a constant-time path must
// not interleave with itself.

template <typename Type>
using CtCrypto = ::crucible::fixy::fn<
    Type, detail_stance::strict<dim::DimensionAxis::Refinement>, detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with<>, grant::as_secret, detail_stance::strict<dim::DimensionAxis::Protocol>,
    detail_stance::strict<dim::DimensionAxis::Lifetime>, detail_stance::strict<dim::DimensionAxis::Provenance>,
    detail_stance::strict<dim::DimensionAxis::Trust>, detail_stance::strict<dim::DimensionAxis::Representation>,
    detail_stance::strict<dim::DimensionAxis::Observability>, detail_stance::strict<dim::DimensionAxis::Complexity>,
    detail_stance::strict<dim::DimensionAxis::Precision>, detail_stance::strict<dim::DimensionAxis::Space>,
    detail_stance::strict<dim::DimensionAxis::Overflow>, detail_stance::strict<dim::DimensionAxis::Mutation>,
    detail_stance::strict<dim::DimensionAxis::Reentrancy>, detail_stance::strict<dim::DimensionAxis::Size>,
    detail_stance::strict<dim::DimensionAxis::Version>, detail_stance::strict<dim::DimensionAxis::Staleness>,
    detail_stance::strict<dim::DimensionAxis::Synchronization>, detail_stance::strict<dim::DimensionAxis::Regime>,
    detail_stance::strict<dim::DimensionAxis::FpMode>, detail_stance::strict<dim::DimensionAxis::SyscallSurface>,
    detail_stance::strict<dim::DimensionAxis::ControlFlow>, detail_stance::strict<dim::DimensionAxis::CallShape>,
    detail_stance::strict<dim::DimensionAxis::StackUse>, detail_stance::strict<dim::DimensionAxis::GlobalState>,
    detail_stance::strict<dim::DimensionAxis::Stdio>, detail_stance::strict<dim::DimensionAxis::HwInstruction>,
    detail_stance::strict<dim::DimensionAxis::BarrierStrength>, detail_stance::strict<dim::DimensionAxis::SimdIsa>,
    detail_stance::strict<dim::DimensionAxis::MemoryScope>>;

// The Security axis takes a declassification rather than a plain public pin.
// Both land the binding at Public, but only the declassification carries the
// policy that authorized the emission, and `policy_t` recovers it from the
// type.  A plain pin would leave the emission auditable only by call site.

template <typename Type, typename Policy>
using PublicEmit = ::crucible::fixy::fn<
    Type, detail_stance::strict<dim::DimensionAxis::Refinement>, detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with_io, grant::declassify<Policy>, detail_stance::strict<dim::DimensionAxis::Protocol>,
    detail_stance::strict<dim::DimensionAxis::Lifetime>, detail_stance::strict<dim::DimensionAxis::Provenance>,
    detail_stance::strict<dim::DimensionAxis::Trust>, detail_stance::strict<dim::DimensionAxis::Representation>,
    detail_stance::strict<dim::DimensionAxis::Observability>, detail_stance::strict<dim::DimensionAxis::Complexity>,
    detail_stance::strict<dim::DimensionAxis::Precision>, detail_stance::strict<dim::DimensionAxis::Space>,
    detail_stance::strict<dim::DimensionAxis::Overflow>, detail_stance::strict<dim::DimensionAxis::Mutation>,
    detail_stance::strict<dim::DimensionAxis::Reentrancy>, detail_stance::strict<dim::DimensionAxis::Size>,
    detail_stance::strict<dim::DimensionAxis::Version>, detail_stance::strict<dim::DimensionAxis::Staleness>,
    detail_stance::strict<dim::DimensionAxis::Synchronization>, detail_stance::strict<dim::DimensionAxis::Regime>,
    detail_stance::strict<dim::DimensionAxis::FpMode>, detail_stance::strict<dim::DimensionAxis::SyscallSurface>,
    detail_stance::strict<dim::DimensionAxis::ControlFlow>, detail_stance::strict<dim::DimensionAxis::CallShape>,
    detail_stance::strict<dim::DimensionAxis::StackUse>, detail_stance::strict<dim::DimensionAxis::GlobalState>,
    detail_stance::strict<dim::DimensionAxis::Stdio>, detail_stance::strict<dim::DimensionAxis::HwInstruction>,
    detail_stance::strict<dim::DimensionAxis::BarrierStrength>, detail_stance::strict<dim::DimensionAxis::SimdIsa>,
    detail_stance::strict<dim::DimensionAxis::MemoryScope>>;

// Security is pinned here rather than left strict, for the same reason as any
// other IO binding.  A secret-carrying async endpoint composes a
// declassification with the IO and coroutine grants instead.
template <typename Type>
using AsyncEndpoint = ::crucible::fixy::fn<
    Type, detail_stance::strict<dim::DimensionAxis::Refinement>, detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with_io, grant::as_public, detail_stance::strict<dim::DimensionAxis::Protocol>,
    detail_stance::strict<dim::DimensionAxis::Lifetime>, detail_stance::strict<dim::DimensionAxis::Provenance>,
    detail_stance::strict<dim::DimensionAxis::Trust>, detail_stance::strict<dim::DimensionAxis::Representation>,
    detail_stance::strict<dim::DimensionAxis::Observability>, detail_stance::strict<dim::DimensionAxis::Complexity>,
    detail_stance::strict<dim::DimensionAxis::Precision>, detail_stance::strict<dim::DimensionAxis::Space>,
    detail_stance::strict<dim::DimensionAxis::Overflow>, detail_stance::strict<dim::DimensionAxis::Mutation>,
    grant::coroutine, detail_stance::strict<dim::DimensionAxis::Size>,
    detail_stance::strict<dim::DimensionAxis::Version>, detail_stance::strict<dim::DimensionAxis::Staleness>,
    detail_stance::strict<dim::DimensionAxis::Synchronization>, detail_stance::strict<dim::DimensionAxis::Regime>,
    detail_stance::strict<dim::DimensionAxis::FpMode>, detail_stance::strict<dim::DimensionAxis::SyscallSurface>,
    detail_stance::strict<dim::DimensionAxis::ControlFlow>, detail_stance::strict<dim::DimensionAxis::CallShape>,
    detail_stance::strict<dim::DimensionAxis::StackUse>, detail_stance::strict<dim::DimensionAxis::GlobalState>,
    detail_stance::strict<dim::DimensionAxis::Stdio>, detail_stance::strict<dim::DimensionAxis::HwInstruction>,
    detail_stance::strict<dim::DimensionAxis::BarrierStrength>, detail_stance::strict<dim::DimensionAxis::SimdIsa>,
    detail_stance::strict<dim::DimensionAxis::MemoryScope>>;

template <typename Type, typename Proto>
using NamedSession = ::crucible::fixy::fn<
    Type, detail_stance::strict<dim::DimensionAxis::Refinement>, detail_stance::strict<dim::DimensionAxis::Usage>,
    detail_stance::strict<dim::DimensionAxis::Effect>, detail_stance::strict<dim::DimensionAxis::Security>,
    grant::protocol<Proto>, detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>, detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>, detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>, detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>, detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>, detail_stance::strict<dim::DimensionAxis::Reentrancy>,
    detail_stance::strict<dim::DimensionAxis::Size>, detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>, detail_stance::strict<dim::DimensionAxis::Synchronization>,
    detail_stance::strict<dim::DimensionAxis::Regime>, detail_stance::strict<dim::DimensionAxis::FpMode>,
    detail_stance::strict<dim::DimensionAxis::SyscallSurface>, detail_stance::strict<dim::DimensionAxis::ControlFlow>,
    detail_stance::strict<dim::DimensionAxis::CallShape>, detail_stance::strict<dim::DimensionAxis::StackUse>,
    detail_stance::strict<dim::DimensionAxis::GlobalState>, detail_stance::strict<dim::DimensionAxis::Stdio>,
    detail_stance::strict<dim::DimensionAxis::HwInstruction>,
    detail_stance::strict<dim::DimensionAxis::BarrierStrength>, detail_stance::strict<dim::DimensionAxis::SimdIsa>,
    detail_stance::strict<dim::DimensionAxis::MemoryScope>>;

// There is no stance for a coroutine in a background context.  A coroutine
// declared under Bg can suspend on one background thread and resume on
// another, so the combination is unsound whatever else the binding pins.
// Cooperative background work goes through an executor protocol on the
// Protocol axis instead of a Reentrancy grant.

// Carrying Block alongside IO puts the blocking nature in the signature, where
// a hot-path admission check can refuse it.  Reentrancy stays strict, since a
// blocking call cannot interleave with itself at the same frame.

template <typename Type>
using SyncBlocking = ::crucible::fixy::fn<
    Type, detail_stance::strict<dim::DimensionAxis::Refinement>, detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with<effects::Effect::IO, effects::Effect::Block>, grant::as_public,
    detail_stance::strict<dim::DimensionAxis::Protocol>, detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>, detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>, detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>, detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>, detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>, detail_stance::strict<dim::DimensionAxis::Reentrancy>,
    detail_stance::strict<dim::DimensionAxis::Size>, detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>, detail_stance::strict<dim::DimensionAxis::Synchronization>,
    detail_stance::strict<dim::DimensionAxis::Regime>, detail_stance::strict<dim::DimensionAxis::FpMode>,
    detail_stance::strict<dim::DimensionAxis::SyscallSurface>, detail_stance::strict<dim::DimensionAxis::ControlFlow>,
    detail_stance::strict<dim::DimensionAxis::CallShape>, detail_stance::strict<dim::DimensionAxis::StackUse>,
    detail_stance::strict<dim::DimensionAxis::GlobalState>, detail_stance::strict<dim::DimensionAxis::Stdio>,
    detail_stance::strict<dim::DimensionAxis::HwInstruction>,
    detail_stance::strict<dim::DimensionAxis::BarrierStrength>, detail_stance::strict<dim::DimensionAxis::SimdIsa>,
    detail_stance::strict<dim::DimensionAxis::MemoryScope>>;

// The empty Effect row is spelled out rather than taken from the strict
// default.  Both resolve to the same row today, and that is the point: a later
// widening of the default would relax every stance that only accepts it, while
// this one keeps the hot-loop discipline of no IO, no allocation, no blocking.

template <typename Type>
using RealtimeHot = ::crucible::fixy::fn<
    Type, detail_stance::strict<dim::DimensionAxis::Refinement>, detail_stance::strict<dim::DimensionAxis::Usage>,
    grant::with<>, grant::as_public, detail_stance::strict<dim::DimensionAxis::Protocol>,
    detail_stance::strict<dim::DimensionAxis::Lifetime>, detail_stance::strict<dim::DimensionAxis::Provenance>,
    detail_stance::strict<dim::DimensionAxis::Trust>, detail_stance::strict<dim::DimensionAxis::Representation>,
    detail_stance::strict<dim::DimensionAxis::Observability>, detail_stance::strict<dim::DimensionAxis::Complexity>,
    detail_stance::strict<dim::DimensionAxis::Precision>, detail_stance::strict<dim::DimensionAxis::Space>,
    detail_stance::strict<dim::DimensionAxis::Overflow>, detail_stance::strict<dim::DimensionAxis::Mutation>,
    detail_stance::strict<dim::DimensionAxis::Reentrancy>, detail_stance::strict<dim::DimensionAxis::Size>,
    detail_stance::strict<dim::DimensionAxis::Version>, detail_stance::strict<dim::DimensionAxis::Staleness>,
    detail_stance::strict<dim::DimensionAxis::Synchronization>, detail_stance::strict<dim::DimensionAxis::Regime>,
    detail_stance::strict<dim::DimensionAxis::FpMode>, detail_stance::strict<dim::DimensionAxis::SyscallSurface>,
    detail_stance::strict<dim::DimensionAxis::ControlFlow>, detail_stance::strict<dim::DimensionAxis::CallShape>,
    detail_stance::strict<dim::DimensionAxis::StackUse>, detail_stance::strict<dim::DimensionAxis::GlobalState>,
    detail_stance::strict<dim::DimensionAxis::Stdio>, detail_stance::strict<dim::DimensionAxis::HwInstruction>,
    detail_stance::strict<dim::DimensionAxis::BarrierStrength>, detail_stance::strict<dim::DimensionAxis::SimdIsa>,
    detail_stance::strict<dim::DimensionAxis::MemoryScope>>;

// The two stances below exist so that every SecLevel is reachable through some
// stance.  Without them a caller wanting Internal or Unclassified has to write
// the whole axis pack out by hand, which drifts from the stance-canonical
// choices on all the other axes.

template <typename Type>
using InternalApi = ::crucible::fixy::fn<
    Type, detail_stance::strict<dim::DimensionAxis::Refinement>, detail_stance::strict<dim::DimensionAxis::Usage>,
    detail_stance::strict<dim::DimensionAxis::Effect>, grant::as_internal,
    detail_stance::strict<dim::DimensionAxis::Protocol>, detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>, detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>, detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>, detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>, detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>, detail_stance::strict<dim::DimensionAxis::Reentrancy>,
    detail_stance::strict<dim::DimensionAxis::Size>, detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>, detail_stance::strict<dim::DimensionAxis::Synchronization>,
    detail_stance::strict<dim::DimensionAxis::Regime>, detail_stance::strict<dim::DimensionAxis::FpMode>,
    detail_stance::strict<dim::DimensionAxis::SyscallSurface>, detail_stance::strict<dim::DimensionAxis::ControlFlow>,
    detail_stance::strict<dim::DimensionAxis::CallShape>, detail_stance::strict<dim::DimensionAxis::StackUse>,
    detail_stance::strict<dim::DimensionAxis::GlobalState>, detail_stance::strict<dim::DimensionAxis::Stdio>,
    detail_stance::strict<dim::DimensionAxis::HwInstruction>,
    detail_stance::strict<dim::DimensionAxis::BarrierStrength>, detail_stance::strict<dim::DimensionAxis::SimdIsa>,
    detail_stance::strict<dim::DimensionAxis::MemoryScope>>;

template <typename Type>
using UnclassifiedScratch = ::crucible::fixy::fn<
    Type, detail_stance::strict<dim::DimensionAxis::Refinement>, detail_stance::strict<dim::DimensionAxis::Usage>,
    detail_stance::strict<dim::DimensionAxis::Effect>, grant::as_unclassified,
    detail_stance::strict<dim::DimensionAxis::Protocol>, detail_stance::strict<dim::DimensionAxis::Lifetime>,
    detail_stance::strict<dim::DimensionAxis::Provenance>, detail_stance::strict<dim::DimensionAxis::Trust>,
    detail_stance::strict<dim::DimensionAxis::Representation>, detail_stance::strict<dim::DimensionAxis::Observability>,
    detail_stance::strict<dim::DimensionAxis::Complexity>, detail_stance::strict<dim::DimensionAxis::Precision>,
    detail_stance::strict<dim::DimensionAxis::Space>, detail_stance::strict<dim::DimensionAxis::Overflow>,
    detail_stance::strict<dim::DimensionAxis::Mutation>, detail_stance::strict<dim::DimensionAxis::Reentrancy>,
    detail_stance::strict<dim::DimensionAxis::Size>, detail_stance::strict<dim::DimensionAxis::Version>,
    detail_stance::strict<dim::DimensionAxis::Staleness>, detail_stance::strict<dim::DimensionAxis::Synchronization>,
    detail_stance::strict<dim::DimensionAxis::Regime>, detail_stance::strict<dim::DimensionAxis::FpMode>,
    detail_stance::strict<dim::DimensionAxis::SyscallSurface>, detail_stance::strict<dim::DimensionAxis::ControlFlow>,
    detail_stance::strict<dim::DimensionAxis::CallShape>, detail_stance::strict<dim::DimensionAxis::StackUse>,
    detail_stance::strict<dim::DimensionAxis::GlobalState>, detail_stance::strict<dim::DimensionAxis::Stdio>,
    detail_stance::strict<dim::DimensionAxis::HwInstruction>,
    detail_stance::strict<dim::DimensionAxis::BarrierStrength>, detail_stance::strict<dim::DimensionAxis::SimdIsa>,
    detail_stance::strict<dim::DimensionAxis::MemoryScope>>;

}  // namespace stance

namespace detail::fn_self_test {

static_assert(std::is_same_v<typename stance::PureLinear<int>::safety_fn_t, safety::fn::Fn<int>>,
              "stance::PureLinear<int>::safety_fn_t must round-trip to "
              "safety::fn::Fn<int>'s all-default instantiation.");

static_assert(sizeof(stance::PureLinear<int>) == sizeof(int),
              "stance::PureLinear<int> must collapse to sizeof(int).  Every grant "
              "tag is empty, so the axis pack carries no runtime state.");
static_assert(sizeof(stance::PureLinear<char>) == sizeof(char),
              "stance::PureLinear<char> must collapse to sizeof(char).");
static_assert(sizeof(stance::PureLinear<double>) == sizeof(double),
              "stance::PureLinear<double> must collapse to sizeof(double).");

static_assert(
    detail::resolve::resolve_usage_v<grant::accept_default_strict_for<dim::DimensionAxis::Refinement>, grant::affine>
        == safety::fn::UsageMode::Affine,
    "grant::affine must project to UsageMode::Affine.");

static_assert(
    std::is_same_v<
        detail::resolve::resolve_refinement_t<grant::accept_default_strict_for<dim::DimensionAxis::Refinement>>,
        safety::fn::pred::True>,
    "accept_default_strict_for<Refinement> must project to "
    "pred::True (the substrate's Refinement default).");

static_assert(stance::PureCopy<int>::usage_v == safety::fn::UsageMode::Copy,
              "stance::PureCopy must resolve Usage to Copy.");
static_assert(std::is_same_v<typename stance::PureCopy<int>::refinement_t, safety::fn::pred::True>,
              "stance::PureCopy must keep Refinement at the strict default.");

static_assert(std::is_same_v<typename stance::IoFunction<int>::effect_row_t, effects::Row<effects::Effect::IO>>,
              "stance::IoFunction's Effect row must contain exactly Effect::IO.");

static_assert(stance::AsyncEndpoint<int>::reentrancy_v == safety::fn::ReentrancyMode::Coroutine,
              "stance::AsyncEndpoint must resolve Reentrancy to Coroutine.");

namespace round_trip_2 {
using direct_fixy = ::crucible::fixy::fn<int, grant::accept_default_strict_for<dim::DimensionAxis::Refinement>,
                                         grant::affine, grant::accept_default_strict_for<dim::DimensionAxis::Effect>,
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
                                         grant::accept_default_strict_for<dim::DimensionAxis::Synchronization>,
                                         grant::accept_default_strict_for<dim::DimensionAxis::Regime>,
                                         grant::accept_default_strict_for<dim::DimensionAxis::FpMode>,
                                         grant::accept_default_strict_for<dim::DimensionAxis::SyscallSurface>,
                                         grant::accept_default_strict_for<dim::DimensionAxis::ControlFlow>,
                                         grant::accept_default_strict_for<dim::DimensionAxis::CallShape>,
                                         grant::accept_default_strict_for<dim::DimensionAxis::StackUse>,
                                         grant::accept_default_strict_for<dim::DimensionAxis::GlobalState>,
                                         grant::accept_default_strict_for<dim::DimensionAxis::Stdio>,
                                         grant::accept_default_strict_for<dim::DimensionAxis::HwInstruction>,
                                         grant::accept_default_strict_for<dim::DimensionAxis::BarrierStrength>,
                                         grant::accept_default_strict_for<dim::DimensionAxis::SimdIsa>,
                                         grant::accept_default_strict_for<dim::DimensionAxis::MemoryScope>>;

using direct_substrate = safety::fn::Fn<
    int, safety::fn::pred::True, safety::fn::UsageMode::Affine, effects::Row<>, safety::fn::SecLevel::Classified,
    safety::fn::proto::None, safety::fn::lifetime::Static, safety::source::FromInternal, safety::trust::Unverified,
    safety::fn::ReprKind::Opaque, safety::fn::cost::Unstated, safety::fn::precision::Exact, safety::fn::space::Zero,
    safety::fn::OverflowMode::Trap, safety::fn::MutationMode::Immutable, safety::fn::ReentrancyMode::NonReentrant,
    safety::fn::size_pol::Unstated, 1u, safety::fn::stale::Fresh>;

static_assert(std::is_same_v<direct_fixy::safety_fn_t, direct_substrate>,
              "Single-relaxation round-trip: fixy::fn's safety_fn_t with "
              "Usage=affine must match the directly-spelled substrate Fn<...> "
              "with UsageMode::Affine.");
}  // namespace round_trip_2

static_assert(detail::resolve::project<grant::as_unclassified>::value == safety::fn::SecLevel::Unclassified,
              "grant::as_unclassified must project to SecLevel::Unclassified.");
static_assert(detail::resolve::project<grant::as_public>::value == safety::fn::SecLevel::Public,
              "grant::as_public must project to SecLevel::Public.");
static_assert(detail::resolve::project<grant::as_internal>::value == safety::fn::SecLevel::Internal,
              "grant::as_internal must project to SecLevel::Internal.");
static_assert(detail::resolve::project<grant::as_classified>::value == safety::fn::SecLevel::Classified,
              "grant::as_classified must project to SecLevel::Classified.");
static_assert(detail::resolve::project<grant::as_secret>::value == safety::fn::SecLevel::Secret,
              "grant::as_secret must project to SecLevel::Secret.");

static_assert(std::is_same_v<detail::resolve::project<grant::trust_verified>::type, safety::trust::Verified>,
              "grant::trust_verified must project to safety::trust::Verified.");
static_assert(std::is_same_v<detail::resolve::project<grant::trust_tested>::type, safety::trust::Tested>,
              "grant::trust_tested must project to safety::trust::Tested.");
static_assert(std::is_same_v<detail::resolve::project<grant::trust_unverified>::type, safety::trust::Unverified>,
              "grant::trust_unverified must project to safety::trust::Unverified.");
static_assert(std::is_same_v<detail::resolve::project<grant::trust_external>::type, safety::trust::External>,
              "grant::trust_external must project to safety::trust::External.");

static_assert(detail::resolve::project<grant::affine>::value == safety::fn::UsageMode::Affine,
              "grant::affine must project to UsageMode::Affine.");
static_assert(detail::resolve::project<grant::copy>::value == safety::fn::UsageMode::Copy,
              "grant::copy must project to UsageMode::Copy.");
static_assert(detail::resolve::project<grant::ghost>::value == safety::fn::UsageMode::Ghost,
              "grant::ghost must project to UsageMode::Ghost.");
static_assert(detail::resolve::project<grant::borrow>::value == safety::fn::UsageMode::Borrow,
              "grant::borrow must project to UsageMode::Borrow.");
static_assert(detail::resolve::project<grant::capability_usage>::value == safety::fn::UsageMode::Capability,
              "grant::capability_usage must project to UsageMode::Capability.  "
              "The `_usage` suffix keeps the grant tag distinct from the "
              "Effect-axis Capability class template.");

// Four of the five Usage-axis grants spell their UsageMode enumerator as a bare
// noun.  The fifth takes a `_usage` suffix because a Capability name already
// exists elsewhere in the namespace tree.  A bare `grant::capability` would
// compile, since the namespaces disambiguate, but the two spellings would then
// be indistinguishable to a grep over the grant roster.  The witnesses below
// hold the distinction structurally rather than by convention.

namespace found_043_witness {

// UsageMode::Linear is the strict default and has no relaxation grant of its
// own, so it is absent from this roster.
using AllUsageGrants = std::tuple<grant::affine, grant::copy, grant::ghost, grant::borrow, grant::capability_usage>;

inline constexpr std::size_t kUsageGrantCount = std::tuple_size_v<AllUsageGrants>;
static_assert(kUsageGrantCount == 5, "Cardinality pin: there are 5 non-default Usage-axis grants.  "
                                     "UsageMode has 6 enumerators, and Linear is the strict default "
                                     "with no relaxation grant.  A sixth grant must be appended to "
                                     "AllUsageGrants and this literal incremented together.");

template <typename Tuple>
[[nodiscard]] consteval bool all_route_to_usage_axis() noexcept {
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        return ((grant::which_dim_v<std::tuple_element_t<Is, Tuple>> == dim::DimensionAxis::Usage) && ...);
    }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}
static_assert(all_route_to_usage_axis<AllUsageGrants>(),
              "Every Usage-axis grant in AllUsageGrants must specialize "
              "which_dim to DimensionAxis::Usage.  An unspecialized grant falls "
              "through to the Type axis, which silently re-classifies it.");

// The witness needs one valid (Effect, context) pair.  Which pair does not
// matter.
using EffectAxisCapability =
    ::crucible::effects::Capability<::crucible::effects::Effect::Alloc, ::crucible::effects::Bg>;

static_assert(grant::IsGrantTag<grant::capability_usage>, "grant::capability_usage must satisfy IsGrantTag: it "
                                                          "derives grant_base and is final.");
static_assert(!grant::IsGrantTag<EffectAxisCapability>, "effects::Capability<E, S> must not satisfy IsGrantTag.  It "
                                                        "is the linear proof-token carrier on the Effect axis, "
                                                        "structurally distinct from the Usage-axis "
                                                        "grant::capability_usage relaxation tag.  The two "
                                                        "Capability spellings coexist because one is a class "
                                                        "template and the other is a suffixed grant tag.");

// The re-exports of this capability template elsewhere in the tree are pinned
// identical at their own definition sites.  Restating that here would need an
// include that closes a dependency cycle.

}  // namespace found_043_witness

constexpr auto minted = mint_fn<int, grant::accept_default_strict_for<dim::DimensionAxis::Refinement>,
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
                                grant::accept_default_strict_for<dim::DimensionAxis::Synchronization>,
                                grant::accept_default_strict_for<dim::DimensionAxis::Regime>,
                                grant::accept_default_strict_for<dim::DimensionAxis::FpMode>,
                                grant::accept_default_strict_for<dim::DimensionAxis::SyscallSurface>,
                                grant::accept_default_strict_for<dim::DimensionAxis::ControlFlow>,
                                grant::accept_default_strict_for<dim::DimensionAxis::CallShape>,
                                grant::accept_default_strict_for<dim::DimensionAxis::StackUse>,
                                grant::accept_default_strict_for<dim::DimensionAxis::GlobalState>,
                                grant::accept_default_strict_for<dim::DimensionAxis::Stdio>,
                                grant::accept_default_strict_for<dim::DimensionAxis::HwInstruction>,
                                grant::accept_default_strict_for<dim::DimensionAxis::BarrierStrength>,
                                grant::accept_default_strict_for<dim::DimensionAxis::SimdIsa>,
                                grant::accept_default_strict_for<dim::DimensionAxis::MemoryScope>>(42);
static_assert(minted.value() == 42, "mint_fn must construct fixy::fn carrying the supplied value.");

namespace fixy_u_041 {
struct FakeProto {};
}  // namespace fixy_u_041

static_assert(
    std::is_same_v<typename stance::NamedSession<int, fixy_u_041::FakeProto>::protocol_t, fixy_u_041::FakeProto>,
    "stance::NamedSession<int, Proto>::protocol_t must thread Proto "
    "through to the substrate's protocol_t.");

static_assert(sizeof(stance::NamedSession<int, fixy_u_041::FakeProto>) == sizeof(int),
              "stance::NamedSession<int, Proto> must EBO-collapse to sizeof(int) "
              "— grant::protocol<Proto> is an empty type-level tag.");

static_assert(std::is_same_v<typename stance::SyncBlocking<int>::effect_row_t,
                             effects::Row<effects::Effect::IO, effects::Effect::Block>>,
              "stance::SyncBlocking's Effect row must contain IO and Block.");

static_assert(stance::SyncBlocking<int>::security_v == safety::fn::SecLevel::Public,
              "stance::SyncBlocking must resolve Security to Public via "
              "grant::as_public.");

static_assert(sizeof(stance::SyncBlocking<int>) == sizeof(int),
              "stance::SyncBlocking<int> must EBO-collapse to sizeof(int).");

static_assert(std::is_same_v<typename stance::RealtimeHot<int>::effect_row_t, effects::Row<>>,
              "stance::RealtimeHot's Effect row must be empty — hot-loop "
              "discipline forbids IO/Alloc/Block at the signature.");

static_assert(stance::RealtimeHot<int>::security_v == safety::fn::SecLevel::Public,
              "stance::RealtimeHot must resolve Security to Public.");

static_assert(sizeof(stance::RealtimeHot<int>) == sizeof(int),
              "stance::RealtimeHot<int> must EBO-collapse to sizeof(int).");

}  // namespace detail::fn_self_test

}  // namespace crucible::fixy

// The contribution below folds the resolved safety_fn_t rather than the Grants
// pack directly.  Resolution selects one grant per axis, so the resolved type is
// the same whichever order the caller wrote the pack in, and routing through it
// inherits that permutation invariance instead of re-deriving it.
//
// The wrapper salt keeps this hash distinct from the one a directly-spelled
// substrate Fn with the same axis resolution produces.  Collapsing the two onto
// one cache slot would erase the record of which surface published a kernel, and
// the federation cache key is the only place that record survives.

#include <crucible/safety/diag/RowHashFold.h>

namespace crucible::safety::diag {

// The fixy-only axes do not project onto the substrate Fn surface, so two
// bindings that differ only on one of them resolve to the same safety_fn_t and
// would share a federation cache slot.  A peer downloading the safe kernel would
// receive the privileged one.  Folding each grant's stable_type_id in canonical
// axis order separates them: the id identifies the grant's (axis, value)
// position, and a fixed axis order makes the result independent of the order the
// caller wrote the pack in.
namespace detail {

template <typename... Grants>
[[nodiscard]] consteval std::uint64_t fold_canonicalized_grants_hash(std::uint64_t seed) noexcept {
    std::uint64_t h = seed;
    // Every type in `Grants...` must be a grant tag.  The `IsGrantTag_v<Grants>`
    // term below does not make this tolerant of anything else: `which_dim_v` is
    // a variable template, and its instantiation is forced whatever the `&&`
    // operand order, so a non-grant type is a hard error rather than a silent
    // skip.  That is sound because the only callers are the row_hash
    // specialization, whose class body has already rejected any non-grant, and
    // the self-tests, which pass grant tags exclusively.  The term stays as a
    // per-axis match guard.
    //
    // The loop starts at the first fixy-only axis rather than at zero.  The axes
    // below it project onto the substrate Fn surface and are already folded into
    // the seed, so covering them again is redundant.  For the Effect axis it is
    // also wrong: `with<Bg, Alloc>` and `with<Alloc, Bg>` are distinct types with
    // distinct ids, and folding them would break the permutation invariance the
    // canonicalized row already guarantees.  Every fixy-only grant is
    // fixed-arity with positionally distinct arguments, so no such set-semantics
    // question arises for the axes this loop does cover.
    constexpr int kAxisCount = static_cast<int>(::crucible::fixy::dim::DIMENSION_AXIS_COUNT);
    constexpr int kFirstFixyOnly = static_cast<int>(::crucible::fixy::dim::DimensionAxis::Synchronization);
    for (int axis = kFirstFixyOnly; axis < kAxisCount; ++axis) {
        ((::crucible::fixy::grant::IsGrantTag_v<Grants>
                  && static_cast<int>(::crucible::fixy::grant::which_dim_v<Grants>) == axis
              ? (h = detail::combine_ids(h, ::crucible::safety::diag::stable_type_id<Grants>), true)
              : false),
         ...);
    }
    return h;
}

}  // namespace detail

template <typename Type, typename... Grants>
struct row_hash_contribution<::crucible::fixy::fn<Type, Grants...>> {
    static constexpr std::uint64_t value = []() consteval -> std::uint64_t {
        std::uint64_t h =
            detail::combine_ids(detail::WRAPPER_FIXY_FN_TAG,
                                row_hash_contribution_v<typename ::crucible::fixy::fn<Type, Grants...>::safety_fn_t>);
        return detail::fold_canonicalized_grants_hash<Grants...>(h);
    }();
};

namespace detail::fixy_fn_row_hash_self_test {

using crucible::fixy::stance::PureCopy;
using crucible::fixy::stance::PureLinear;
using crucible::fixy::stance::IoFunction;
using crucible::fixy::stance::BgWorker;
using crucible::fixy::stance::CtCrypto;

static_assert(row_hash_contribution_v<PureCopy<int>> != 0, "PureCopy<int> must contribute a non-zero federation cache "
                                                           "RowHash.");
static_assert(row_hash_contribution_v<IoFunction<int>> != 0,
              "IoFunction<int> must contribute a non-zero federation cache "
              "RowHash.");

static_assert(row_hash_contribution_v<PureCopy<int>> != row_hash_contribution_v<IoFunction<int>>,
              "PureCopy<int> and IoFunction<int> must produce distinct RowHash "
              "values so the federation cache routes them to disjoint slots.  "
              "They carry the same payload type and differ only in capability.");

static_assert(row_hash_contribution_v<PureLinear<int>> != row_hash_contribution_v<PureCopy<int>>,
              "PureLinear (Usage=Linear) vs PureCopy (Usage=Copy) differ on "
              "the Usage axis — distinct row hashes required.");

static_assert(row_hash_contribution_v<BgWorker<int>> != row_hash_contribution_v<IoFunction<int>>,
              "BgWorker (Effect={Bg,Alloc}) vs IoFunction (Effect={IO}) differ "
              "on the EffectRow axis — distinct row hashes required.");

static_assert(row_hash_contribution_v<CtCrypto<int>> != row_hash_contribution_v<PureLinear<int>>,
              "CtCrypto (constant-time discipline + Security tier) vs "
              "PureLinear differ on multiple axes — distinct row hashes required.");

static_assert(row_hash_contribution_v<PureLinear<int>>
                  != row_hash_contribution_v<typename PureLinear<int>::safety_fn_t>,
              "fixy::fn<T, ...> and the directly-spelled substrate Fn<T, ...> "
              "must route to distinct cache slots, so that the federation cache "
              "lookup preserves which surface published the kernel.");

static_assert(row_hash_contribution_v<int> == 0);
static_assert(row_hash_contribution_v<PureLinear<int>> != 0);

static_assert(!row_hash_of_v<PureLinear<int>>.is_sentinel());

static_assert(detail::fold_canonicalized_grants_hash<::crucible::fixy::grant::hw::msr<0x10u>>(0ULL)
                  != detail::fold_canonicalized_grants_hash<::crucible::fixy::grant::hw::msr<0x20u>>(0ULL),
              "Distinct MSR identifiers on the HwInstruction axis must produce "
              "distinct grant-fold contributions.  Equal contributions let one "
              "kernel be substituted for another at the federation cache.");

static_assert(detail::fold_canonicalized_grants_hash<::crucible::fixy::grant::hw::msr<0x10u>>(0ULL)
                  != detail::fold_canonicalized_grants_hash<::crucible::fixy::grant::accept_default_strict_for<
                      ::crucible::fixy::dim::DimensionAxis::HwInstruction>>(0ULL),
              "A privileged hw::msr grant and the strict HwInstruction default "
              "must route to distinct federation cache slots.  Without the "
              "separation, a peer that downloads the default-safe kernel can "
              "receive the privileged-register bytes instead.");

static_assert(detail::fold_canonicalized_grants_hash<>(0xDEADBEEFULL) == 0xDEADBEEFULL,
              "An empty Grants pack must fold to identity and leave the seed "
              "unchanged.");

static_assert(
    detail::fold_canonicalized_grants_hash<
        ::crucible::fixy::grant::hw::msr<0x10u>,
        ::crucible::fixy::grant::accept_default_strict_for<::crucible::fixy::dim::DimensionAxis::SimdIsa>>(0ULL)
        == detail::fold_canonicalized_grants_hash<
            ::crucible::fixy::grant::accept_default_strict_for<::crucible::fixy::dim::DimensionAxis::SimdIsa>,
            ::crucible::fixy::grant::hw::msr<0x10u>>(0ULL),
    "The grant fold must be ordered by canonical axis, so that source-order "
    "permutations of one Grants pack collapse to the same federation cache "
    "slot.");

// The row a `with<Es...>` grant projects to already hashes independently of the
// order of its effects.  That does not by itself give the same property here:
// it holds at this level only while the wrapper salt rides on the canonicalized
// row rather than on the grant pack.  The witness below pins that it does.
//
// The engagement count is a separate question.  `with<Bg, Alloc>` is one grant
// and therefore one Effect engagement, whichever order it lists.  The
// acceptance gate counts grants per axis, not effects within a grant.  It
// rejects two separate `with<>` grants in one pack, and both invariants hold
// at once.

namespace found_045_witness {

template <typename Type>
using BgWorker_AllocBg = ::crucible::fixy::fn<
    Type, ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Refinement>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Usage>,
    // The one line that differs from BgWorker: the Effect pack is reversed.
    ::crucible::fixy::grant::with<::crucible::effects::Effect::Alloc, ::crucible::effects::Effect::Bg>,
    ::crucible::fixy::grant::as_public,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Protocol>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Lifetime>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Provenance>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Trust>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Representation>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Observability>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Complexity>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Precision>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Space>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Overflow>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Mutation>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Reentrancy>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Size>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Version>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Staleness>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Synchronization>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Regime>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::FpMode>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::SyscallSurface>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::ControlFlow>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::CallShape>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::StackUse>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::GlobalState>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::Stdio>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::HwInstruction>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::BarrierStrength>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::SimdIsa>,
    ::crucible::fixy::stance::detail_stance::strict<::crucible::fixy::dim::DimensionAxis::MemoryScope>>;

static_assert(row_hash_contribution_v<BgWorker<int>> == row_hash_contribution_v<BgWorker_AllocBg<int>>,
              "A fixy::fn with a permuted `with<Es...>` Effect pack must "
              "produce the same row_hash_contribution, because the two packs "
              "name the same Effect row.  If this fires, the wrapper salt is "
              "being mixed in before row canonicalization.  Flip the order so "
              "that the salt rides on the canonicalized row.");

static_assert(row_hash_contribution_v<BgWorker_AllocBg<int>> != 0,
              "BgWorker_AllocBg<int> must contribute a non-zero row hash, so "
              "that the equality above is not satisfied by both sides being "
              "zero.");

static_assert(row_hash_contribution_v<BgWorker_AllocBg<int>> != row_hash_contribution_v<IoFunction<int>>,
              "The permuted BgWorker must still differ from IoFunction: their "
              "Effect rows are different.");

}  // namespace found_045_witness

}  // namespace detail::fixy_fn_row_hash_self_test

}  // namespace crucible::safety::diag
