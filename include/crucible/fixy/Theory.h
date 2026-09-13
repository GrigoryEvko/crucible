#pragma once

// Retiring an entry means deleting it from `CorpusEntries` and from
// `kRoster`.  Marking `matches()` `[[deprecated]]` retires nothing: the
// fold still evaluates it, so the binding is still rejected, and the
// warning fires once per instantiation that reaches this header rather
// than once per binding the retired entry concerns.

#include <crucible/fixy/Default.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/Secret.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::fixy::theory {

namespace detail {

template <template <typename> class Predicate, typename... Grants>
[[nodiscard]] consteval bool has_grant_of() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return false;
    } else {
        return (Predicate<Grants>::value || ...);
    }
}

template <typename G>
struct is_secret_grant : std::false_type {};
template <>
struct is_secret_grant<grant::as_secret> : std::true_type {};
template <>
struct is_secret_grant<grant::as_classified> : std::true_type {};
template <typename Policy>
struct is_secret_grant<grant::declassify<Policy>> : std::true_type {};
template <>
struct is_secret_grant<grant::accept_default_strict_for<dim::DimensionAxis::Security>> : std::true_type {};

static_assert(strict_default_for<dim::DimensionAxis::Security>::value == ::crucible::safety::fn::SecLevel::Classified,
              "strict_default_for<Security> must resolve to "
              "SecLevel::Classified.  The deferred-form specialization of "
              "is_secret_grant treats accept_default_strict_for<Security> as "
              "an explicit Classified engagement, and that equivalence fails "
              "if the strict default is weakened.");

template <typename G>
struct is_declassify_grant : std::false_type {};
template <typename Policy>
struct is_declassify_grant<grant::declassify<Policy>> : std::true_type {};

// Carrier-side Security only.  `declassify<Policy>` has no
// specialization here on purpose: it is the discharge side.  A matcher
// of the shape `has_secret && !has_declassify` reading `is_secret_grant`
// self-cancels on a declassify-only pack, because the one grant both
// satisfies the carrier arm and clears the discharge arm.  Reading
// `is_secret_carrier` keeps the two arms independent.
template <typename G>
struct is_secret_carrier : std::false_type {};
template <>
struct is_secret_carrier<grant::as_secret> : std::true_type {};
template <>
struct is_secret_carrier<grant::as_classified> : std::true_type {};
template <>
struct is_secret_carrier<grant::accept_default_strict_for<dim::DimensionAxis::Security>> : std::true_type {};
// A declassification policy authorizes exactly the axis it names.  The
// information-erasure literature is explicit on this: a policy written
// to authorize an export channel says nothing about temporal replay,
// so it must not silence a replay reject.  Hence the per-axis mask
// rather than a single "some declassify is present" test.
//
// `axes_discharged_of<Policy>` defaults to `None` so that a policy
// authored before an axis existed cannot discharge that axis by
// accident.
enum class DischargeAxis : std::uint32_t {
    None = 0u,
    Staleness = 1u << 0,
    IO = 1u << 1,
    Bg = 1u << 2,
    Crash = 1u << 3,
    Reentrancy = 1u << 4,
    Termination = 1u << 5,
    CatastrophicReplay = 1u << 6,
};

[[nodiscard]] constexpr DischargeAxis operator|(DischargeAxis a, DischargeAxis b) noexcept {
    return DischargeAxis{std::to_underlying(a) | std::to_underlying(b)};
}
[[nodiscard]] constexpr DischargeAxis operator&(DischargeAxis a, DischargeAxis b) noexcept {
    return DischargeAxis{std::to_underlying(a) & std::to_underlying(b)};
}
[[nodiscard]] constexpr bool discharge_axis_contains(DischargeAxis mask, DischargeAxis axis) noexcept {
    return (std::to_underlying(mask) & std::to_underlying(axis)) != 0u;
}

template <typename Policy>
struct axes_discharged_of : std::integral_constant<DischargeAxis, DischargeAxis::None> {};
template <typename Policy>
inline constexpr DischargeAxis axes_discharged_of_v = axes_discharged_of<Policy>::value;

template <>
struct axes_discharged_of<::crucible::safety::secret_policy::AuthorizedReplay>
    : std::integral_constant<DischargeAxis, DischargeAxis::Staleness> {};

template <DischargeAxis Axis, typename G>
struct is_declassify_for_axis : std::false_type {};
template <DischargeAxis Axis, typename Policy>
struct is_declassify_for_axis<Axis, grant::declassify<Policy>>
    : std::bool_constant<discharge_axis_contains(axes_discharged_of_v<Policy>, Axis)> {};

template <DischargeAxis Axis, typename... Grants>
[[nodiscard]] consteval bool has_declassify_for_axis() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return false;
    } else {
        return (is_declassify_for_axis<Axis, Grants>::value || ...);
    }
}

static_assert(axes_discharged_of_v<::crucible::safety::secret_policy::AuthorizedReplay> == DischargeAxis::Staleness,
              "AuthorizedReplay must discharge Staleness.  It is the only "
              "policy with a non-None mask, so a change here removes the sole "
              "discharge path for the staleness reject.  Lifting a further "
              "axis means naming a new policy tag and specializing "
              "axes_discharged_of for it.");
static_assert(axes_discharged_of_v<::crucible::safety::secret_policy::AuditedLogging> == DischargeAxis::None,
              "AuditedLogging must stay at DischargeAxis::None.  Lifting an "
              "axis for an already-shipped policy requires an explicit "
              "specialization here and a matching justification where the "
              "policy tag is declared.");

// An implicit None and a deliberate None are indistinguishable, so a
// policy that quietly acquires a discharge mask would silence a reject
// with nobody reviewing the lift.

namespace secret_policy_roster {

using AllPolicies = ::crucible::safety::secret_policy::roster::All;

inline constexpr std::size_t kPolicyRosterCardinality = std::tuple_size_v<AllPolicies>;

static_assert(kPolicyRosterCardinality == 6, "The secret_policy roster no longer holds 6 entries.  A new "
                                             "policy needs an axes_discharged_of specialization, an explicit "
                                             "sentinel below witnessing its expected discharge mask, and this "
                                             "count bumped.");

}  // namespace secret_policy_roster

static_assert(axes_discharged_of_v<::crucible::safety::secret_policy::WireSerialize> == DischargeAxis::None,
              "WireSerialize is a serialization policy, not an axis-discharge "
              "policy.  Lifting it to IO requires an explicit specialization "
              "and a bump of the closed-set count below.");
static_assert(axes_discharged_of_v<::crucible::safety::secret_policy::HashForCompare> == DischargeAxis::None,
              "HashForCompare releases a hash of the value.  It discharges "
              "neither temporal replay nor IO.");
static_assert(axes_discharged_of_v<::crucible::safety::secret_policy::LengthOnly> == DischargeAxis::None,
              "LengthOnly releases only size metadata.  Size is an "
              "information channel, but no axis in DischargeAxis names it, so "
              "the policy discharges nothing.  Typing that channel means "
              "minting a new axis bit and lifting this sentinel.");
static_assert(axes_discharged_of_v<::crucible::safety::secret_policy::UserDisplay> == DischargeAxis::None,
              "UserDisplay is a render policy.  It discharges none of the "
              "axes in DischargeAxis.");

namespace detail::secret_policy_invariants {

template <typename TupleT>
struct non_none_policy_count;

template <typename... Ps>
struct non_none_policy_count<std::tuple<Ps...>>
    : std::integral_constant<std::size_t,
                             ((axes_discharged_of_v<Ps> != DischargeAxis::None ? std::size_t{1} : std::size_t{0}) + ...
                              + std::size_t{0})> {};

template <typename TupleT>
inline constexpr std::size_t non_none_policy_count_v = non_none_policy_count<TupleT>::value;

static_assert(non_none_policy_count_v<secret_policy_roster::AllPolicies> == 1,
              "Exactly one secret_policy in the roster may carry a non-None "
              "axes_discharged_of mask, and that one is AuthorizedReplay for "
              "Staleness.  Lifting a second policy requires bumping this "
              "count and adding a sentinel above that names the axis the "
              "policy becomes authoritative on.");

}  // namespace detail::secret_policy_invariants

template <typename G>
struct is_io_effect_grant : std::false_type {};
template <effects::Effect... Es>
struct is_io_effect_grant<grant::with<Es...>> : std::bool_constant<((Es == effects::Effect::IO) || ...)> {};

// The Internal tier sits below the strict default, so a binding reaches
// it only by writing `as_internal`.  There is no deferred form to
// recognize here, unlike `is_secret_grant` above.
template <typename G>
struct is_internal_grant : std::false_type {};
template <>
struct is_internal_grant<grant::as_internal> : std::true_type {};

template <typename G>
struct is_bg_effect_grant : std::false_type {};
template <effects::Effect... Es>
struct is_bg_effect_grant<grant::with<Es...>> : std::bool_constant<((Es == effects::Effect::Bg) || ...)> {};

template <typename G>
struct is_pure_effect_grant : std::false_type {};
template <>
struct is_pure_effect_grant<grant::with<>> : std::true_type {};
template <>
struct is_pure_effect_grant<grant::accept_default_strict_for<dim::DimensionAxis::Effect>> : std::true_type {};

template <typename G>
inline constexpr bool is_pure_effect_grant_v = is_pure_effect_grant<G>::value;

static_assert(std::is_same_v<typename strict_default_for<dim::DimensionAxis::Effect>::type, effects::Row<>>,
              "strict_default_for<Effect> must resolve to effects::Row<>.  "
              "The deferred-form specialization of is_pure_effect_grant "
              "treats the default as an empty row, and a non-empty default "
              "would route that form to a claim it does not make.");

namespace detail::found_040_witness {

static_assert(is_pure_effect_grant_v<grant::with<>>, "An empty effect pack must satisfy is_pure_effect_grant.");

static_assert(is_pure_effect_grant_v<grant::accept_default_strict_for<dim::DimensionAxis::Effect>>,
              "The deferred Effect-axis form must satisfy "
              "is_pure_effect_grant, on a par with the written empty pack.");

static_assert(!is_pure_effect_grant_v<grant::with<effects::Effect::IO>>,
              "with<IO> declares a non-empty effect row and is not pure.");

static_assert(!is_pure_effect_grant_v<grant::with<effects::Effect::Alloc, effects::Effect::IO>>,
              "Any non-empty effect pack disqualifies the binding from the "
              "pure-effect class.");

static_assert(!is_pure_effect_grant_v<grant::with_io>, "An alias for a non-empty pack must stay non-pure.");

static_assert(!is_pure_effect_grant_v<grant::with_bg>, "An alias for a non-empty pack must stay non-pure.");

static_assert(!is_pure_effect_grant_v<grant::accept_default_strict_for<dim::DimensionAxis::Usage>>,
              "The deferred form is axis-keyed.  A deferred grant on any axis "
              "other than Effect must not match.");

static_assert(!is_pure_effect_grant_v<int>, "The predicate is total over the type universe: a non-grant "
                                            "type falls through to the primary template.");
static_assert(!is_pure_effect_grant_v<grant::as_secret>,
              "A Security-axis grant must not match an Effect-axis predicate.");

}  // namespace detail::found_040_witness

template <typename G>
struct is_stale_grant : std::false_type {};
template <auto TauMax>
struct is_stale_grant<grant::stale_to<TauMax>> : std::true_type {};

// The window argument is an `auto` template parameter, so `stale_to<5>`
// and `stale_to<5ULL>` carry different parameter types.  Widening to
// uint64_t here lets the threshold comparison below compare the two
// against one scale.  A grant that is not a `stale_to` reports 0, which
// no threshold above zero accepts.
template <typename G>
struct stale_to_value {
    static constexpr std::uint64_t value = 0;
};
template <auto N>
struct stale_to_value<grant::stale_to<N>> {
    static constexpr std::uint64_t value = static_cast<std::uint64_t>(N);
};

template <std::uint64_t Threshold, typename... Grants>
[[nodiscard]] consteval bool any_stale_to_at_least() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return false;
    } else {
        return ((stale_to_value<Grants>::value >= Threshold) || ...);
    }
}

// A replay window this wide lets an observer amortize a replay attack
// over many observations of the same value, which is a different risk
// from reading one or two generations of stale state.  The number is a
// conservative choice, not a measured one.
inline constexpr std::uint64_t kStaleToCatastrophic = 1024;

static_assert(stale_to_value<grant::stale_to<5>>::value == 5u,
              "stale_to<N> must surface N widened to uint64_t.  A failure "
              "here means the grant no longer takes the window as an auto "
              "template parameter and the extractor must follow the new "
              "shape.");
static_assert(stale_to_value<grant::stale_to<10000>>::value == 10000u,
              "stale_to<N> must surface N widened to uint64_t for a window "
              "past the catastrophic threshold.");
static_assert(stale_to_value<grant::as_secret>::value == 0u,
              "A grant that makes no staleness claim must surface 0, so that "
              "it cannot satisfy a magnitude threshold.");
static_assert(any_stale_to_at_least<kStaleToCatastrophic, grant::stale_to<5000>>(),
              "A window above kStaleToCatastrophic must satisfy the "
              "magnitude predicate.");
static_assert(!any_stale_to_at_least<kStaleToCatastrophic, grant::stale_to<100>>(),
              "A window below kStaleToCatastrophic must not satisfy the "
              "magnitude predicate.");
static_assert(!any_stale_to_at_least<kStaleToCatastrophic>(),
              "An empty grant pack makes no staleness claim and must not "
              "satisfy the magnitude predicate.");

template <typename G>
struct is_ghost_grant : std::false_type {};
template <>
struct is_ghost_grant<grant::ghost> : std::true_type {};

// Which effects count as observable is decided where the effect atoms
// are declared, not here.  Deferring keeps a newly added atom from
// defaulting to unobservable, which would widen what ghost code is
// allowed to request without anyone deciding to widen it.
template <typename G>
struct is_observable_effect_grant : std::false_type {};
template <effects::Effect... Es>
struct is_observable_effect_grant<grant::with<Es...>> : std::bool_constant<(effects::is_observable<Es>() || ...)> {};

static_assert(!is_observable_effect_grant<grant::with<effects::Effect::Init>>::value,
              "Init must stay outside the observable set.  Moving it in "
              "rejects every ghost binding that participates in "
              "initialization, so it needs its own corpus entry naming the "
              "contradiction it catches.");
static_assert(!is_observable_effect_grant<grant::with<effects::Effect::Test>>::value,
              "Test must stay outside the observable set.  A specification "
              "evaluated under a test harness is legitimate ghost code.");
static_assert(is_observable_effect_grant<grant::with<effects::Effect::Alloc>>::value,
              "Alloc must stay inside the observable set.");
static_assert(is_observable_effect_grant<grant::with<effects::Effect::IO>>::value,
              "IO must stay inside the observable set.");
static_assert(is_observable_effect_grant<grant::with<effects::Effect::Block>>::value,
              "Block must stay inside the observable set.");
static_assert(is_observable_effect_grant<grant::with<effects::Effect::Bg>>::value,
              "Bg must stay inside the observable set.");

template <typename G>
struct is_external_source_grant : std::false_type {};
template <>
struct is_external_source_grant<grant::from_source<::crucible::safety::source::External>> : std::true_type {};

template <typename G>
struct is_trust_verified_grant : std::false_type {};
template <>
struct is_trust_verified_grant<grant::trust_verified> : std::true_type {};

template <typename G>
struct is_trust_assumed_grant : std::false_type {};
template <auto Rationale>
struct is_trust_assumed_grant<grant::trust_assumed<Rationale>> : std::true_type {};

static_assert(is_external_source_grant<grant::from_source<::crucible::safety::source::External>>::value,
              "from_source<External> must stay detectable as the "
              "low-integrity provenance claim.");
static_assert(!is_external_source_grant<grant::from_source<::crucible::safety::source::Sanitized>>::value,
              "from_source<Sanitized> must not match the External detector.  "
              "Sanitized is the discharged end of the retag, not the raw "
              "input end.");
static_assert(is_trust_verified_grant<grant::trust_verified>::value,
              "trust_verified must stay detectable as the high-integrity "
              "claim.");
static_assert(is_trust_assumed_grant<grant::trust_assumed<grant::axis_query_tag>>::value,
              "trust_assumed must match for any rationale argument.  A "
              "failure here means the grant no longer carries a mandatory "
              "rationale.");

template <typename G>
struct is_cost_unbounded_grant : std::false_type {};
template <>
struct is_cost_unbounded_grant<grant::cost_unbounded> : std::true_type {};

static_assert(is_cost_unbounded_grant<grant::cost_unbounded>::value,
              "cost_unbounded must stay detectable as the claim that "
              "execution may not terminate.");
static_assert(!is_cost_unbounded_grant<grant::cost_constant>::value,
              "cost_constant claims a bound on execution and must not match "
              "the unbounded detector.");
static_assert(!is_cost_unbounded_grant<grant::as_secret>::value,
              "A Security-axis grant must not match a Complexity-axis "
              "detector.");

// The acceptance gate that consumes this corpus appends a Type-axis
// deferred grant to the pack before the matchers run, so every matcher
// sees one grant its author never wrote.  Each per-grant predicate must
// therefore be false on that marker: a predicate that matched it would
// read the injection as a claim, either firing a reject nobody earned
// or clearing a discharge arm nobody discharged.  A deferred-form
// specialization must key on its own axis for that to hold.

namespace detail::found_042_witness {

using TypeMarker = grant::accept_default_strict_for<dim::DimensionAxis::Type>;

template <template <typename> class Predicate>
struct PredicateRef {
    static constexpr bool inert_on_marker_v = !Predicate<TypeMarker>::value;
};

using AllCorpusPredicates =
    std::tuple<PredicateRef<is_secret_grant>, PredicateRef<is_declassify_grant>, PredicateRef<is_io_effect_grant>,
               PredicateRef<is_internal_grant>, PredicateRef<is_bg_effect_grant>, PredicateRef<is_pure_effect_grant>,
               PredicateRef<is_stale_grant>, PredicateRef<is_ghost_grant>, PredicateRef<is_observable_effect_grant>,
               PredicateRef<is_external_source_grant>, PredicateRef<is_trust_verified_grant>,
               PredicateRef<is_trust_assumed_grant>, PredicateRef<is_cost_unbounded_grant>>;

template <typename Tuple>
[[nodiscard]] consteval bool all_inert_on_marker() noexcept {
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        return (std::tuple_element_t<Is, Tuple>::inert_on_marker_v && ...);
    }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

static_assert(all_inert_on_marker<AllCorpusPredicates>(),
              "Every per-grant corpus predicate must be false on the Type-axis "
              "deferred grant that the acceptance gate injects.  A predicate "
              "that matches it reads the injection as a claim the author never "
              "made.  A deferred-form specialization must key on its own axis, "
              "never on an unconstrained axis argument.");

inline constexpr std::size_t kAuditedCorpusPredicateCount = std::tuple_size_v<AllCorpusPredicates>;
static_assert(kAuditedCorpusPredicateCount == 13, "The audited-predicate roster no longer holds 13 entries.  Every "
                                                  "predicate the corpus matchers fold through has_grant_of needs a "
                                                  "row above and this count kept in step.");

// The two assertions below follow from the fold above.  Stating them
// separately names the failing predicate in the diagnostic.
static_assert(!is_secret_grant<TypeMarker>::value, "The deferred Security-axis specialization of is_secret_grant "
                                                   "must not match a deferred grant on the Type axis.");
static_assert(!is_pure_effect_grant<TypeMarker>::value,
              "The deferred Effect-axis specialization of is_pure_effect_grant "
              "must not match a deferred grant on the Type axis.");

}  // namespace detail::found_042_witness

template <typename T>
struct is_secret_type : std::false_type {};
template <typename T>
struct is_secret_type<::crucible::safety::Secret<T>> : std::true_type {};

static_assert(is_secret_type<::crucible::safety::Secret<int>>::value,
              "A Secret-wrapped payload must match the payload detector.  A "
              "failure here means the wrapper takes more than one template "
              "argument now and the specialization must follow.");
static_assert(!is_secret_type<int>::value, "An unwrapped payload must not match the payload detector.");
static_assert(!is_secret_type<::crucible::safety::Secret<int>*>::value,
              "A pointer to a Secret must not match.  The pointer value "
              "itself is not classified, and treating it as classified would "
              "need its own decision.");

static_assert(std::meta::enumerators_of(^^::crucible::safety::fn::SecLevel).size() == 5,
              "SecLevel no longer has 5 enumerators.  The Security-engagement "
              "detectors in this header name grant tags explicitly and do not "
              "extend themselves.  Decide whether the new level raises secrecy "
              "and needs its own is_secret_grant specialization, or sits below "
              "the existing levels and needs none, then bump this count.  "
              "Projecting a payload down to a lower level is not a discharge, "
              "so a lower level never silences a reject on its own.");

// Assembling this text in a function-local static inside each entry
// reads as the obvious placement, but a function-local static is
// per-translation-unit, so every including unit re-runs the assembly.
// Keyed on the entry's two accessors, one instantiation serves the
// whole build.
template <auto NameFn, auto CiteFn>
inline constexpr std::string_view kCorpusFullDiagnostic = []() consteval -> std::string_view {
    std::string msg;
    msg += "fixy::fn<Type, Grants...> [tier 5: "
           "NotInTheoryCorpus]: binding matches known-unsoundness "
           "corpus entry: ";
    std::string_view name_sv = NameFn();
    std::string_view cite_sv = CiteFn();
    msg.append(name_sv.data(), name_sv.size());
    msg += ".  ";
    msg.append(cite_sv.data(), cite_sv.size());
    return std::define_static_string(msg);
}();

}  // namespace detail

namespace corpus {

struct classified_io_without_declassify {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_secret = detail::has_grant_of<detail::is_secret_carrier, Grants...>();
        const bool has_io = detail::has_grant_of<detail::is_io_effect_grant, Grants...>();
        const bool has_declassify = detail::has_declassify_for_axis<detail::DischargeAxis::IO, Grants...>();
        return has_secret && has_io && !has_declassify;
    }

    static constexpr std::string_view name() noexcept { return "classified_io_without_declassify"; }

    static constexpr std::string_view cite() noexcept {
        return "Sabelfeld-Myers 2003 (after Volpano-Smith-Irvine 1996 "
               "type-system foundation) — implicit information flow: "
               "classified value flows out of the program via I/O "
               "without a declassification policy.  Insert "
               "grant::declassify<Policy> with a named policy OR drop "
               "the IO effect.";
    }

    static constexpr std::string_view full_diagnostic() noexcept {
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<&name, &cite>;
    }
};

struct classified_bg_without_declassify {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_secret = detail::has_grant_of<detail::is_secret_carrier, Grants...>();
        const bool has_bg = detail::has_grant_of<detail::is_bg_effect_grant, Grants...>();
        const bool has_declassify = detail::has_declassify_for_axis<detail::DischargeAxis::Bg, Grants...>();
        return has_secret && has_bg && !has_declassify;
    }

    static constexpr std::string_view name() noexcept { return "classified_bg_without_declassify"; }

    static constexpr std::string_view cite() noexcept {
        return "Smith-Volpano 1998 / Sabelfeld-Sands 2000 / "
               "Hedin-Sabelfeld 2012 — concurrent information flow: "
               "classified value crosses into a background-thread "
               "context without a declassification policy; the spawn "
               "is itself a scheduler-observable event.  Insert "
               "grant::declassify<Policy> OR drop the Bg effect OR "
               "project Security to a less restrictive level.";
    }

    static constexpr std::string_view full_diagnostic() noexcept {
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<&name, &cite>;
    }
};

struct staleness_secret_without_declassify {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_secret = detail::has_grant_of<detail::is_secret_grant, Grants...>();
        const bool has_stale = detail::has_grant_of<detail::is_stale_grant, Grants...>();
        const bool has_staleness_discharge =
            detail::has_declassify_for_axis<detail::DischargeAxis::Staleness, Grants...>();
        return has_secret && has_stale && !has_staleness_discharge;
    }

    static constexpr std::string_view name() noexcept { return "staleness_secret_without_declassify"; }

    static constexpr std::string_view cite() noexcept {
        return "Hunt-Sands 2008 'Just Forget It' (POPL) — PRIMARY: "
               "formalizes information-erasure semantics and shows a "
               "classified value reachable through a stale-replay "
               "window (stale_to<TauMax>) without a freshness-"
               "discharging declassification policy is semantically a "
               "FAILED erasure — data the policy would require be "
               "forgotten remains observable.  "
               "Supporting: Askarov-Hunt-Sabelfeld-Sands 2008 "
               "(ESORICS) formalizes the timing/replay channel as an "
               "information leak distinct from data-flow channels.  "
               "Supporting: Sabelfeld-Myers 2003 'Language-based "
               "information-flow security' (IEEE J. Sel. Areas) — "
               "the discharge MUST match the axis it authorizes; a "
               "policy for IO export does not discharge temporal "
               "replay.  "
               "(Orientation only: Sabelfeld-Sands 2009 "
               "'Declassification: dimensions and principles' names "
               "the 'when' dimension — a survey that identifies the "
               "axis, NOT a formalization of this pattern.)  "
               "Remediation: insert grant::declassify<secret_policy::"
               "AuthorizedReplay> (the only shipped policy whose "
               "axes_discharged_of mask carries Staleness) OR "
               "drop the stale_to<N> grant (Staleness defaults to "
               "Fresh) OR project Security to a less restrictive "
               "level.  The other declassify policies "
               "(AuditedLogging / WireSerialize / HashForCompare / "
               "LengthOnly / UserDisplay) do NOT silence this matcher "
               "— their authority lies on IO / Security axes, not "
               "Staleness.";
    }

    static constexpr std::string_view full_diagnostic() noexcept {
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<&name, &cite>;
    }
};

struct ghost_runtime_observable {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_ghost = detail::has_grant_of<detail::is_ghost_grant, Grants...>();
        const bool has_observable = detail::has_grant_of<detail::is_observable_effect_grant, Grants...>();
        return has_ghost && has_observable;
    }

    static constexpr std::string_view name() noexcept { return "ghost_runtime_observable"; }

    static constexpr std::string_view cite() noexcept {
        return "Filliâtre-Gondelman-Paskevich 2014 'The Spirit of "
               "Ghost Code' / Leino 2010 'Dafny' — ghost-state "
               "discipline: a binding engaging Usage=Ghost AND any "
               "runtime-observable "
               "effect (Alloc / IO / Block / Bg) is contradictory — "
               "ghost values are erased at compile time and cannot "
               "drive runtime presence.  Drop the ghost Usage marker "
               "OR drop the runtime-observable effects.  Declassify "
               "does not apply (this is a ghost-vs-runtime category "
               "error, not an information-flow channel).";
    }

    static constexpr std::string_view full_diagnostic() noexcept {
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<&name, &cite>;
    }
};

struct internal_io_without_declassify {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_internal = detail::has_grant_of<detail::is_internal_grant, Grants...>();
        const bool has_io = detail::has_grant_of<detail::is_io_effect_grant, Grants...>();
        // The carrier arm may read `is_internal_grant` directly.  That
        // predicate has no declassify specialization, so the two arms
        // cannot be satisfied by one grant the way they can above.
        const bool has_declassify = detail::has_declassify_for_axis<detail::DischargeAxis::IO, Grants...>();
        return has_internal && has_io && !has_declassify;
    }

    static constexpr std::string_view name() noexcept { return "internal_io_without_declassify"; }

    static constexpr std::string_view cite() noexcept {
        return "Bell-LaPadula 1973 / Volpano-Smith-Irvine 1996 / "
               "Sabelfeld-Myers 2003 — no-write-down for Internal "
               "tier: org-internal value flows into an I/O sink "
               "without a declassification policy.  Internal data is "
               "below the strict default (Classified) but ABOVE "
               "Public — every non-Public→Public crossing requires "
               "audit-trail discharge.  Insert grant::declassify"
               "<Policy> with a named organizational-disclosure "
               "policy OR drop the IO effect OR project Security to "
               "as_public / as_unclassified.";
    }

    static constexpr std::string_view full_diagnostic() noexcept {
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<&name, &cite>;
    }
};

struct internal_bg_without_declassify {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_internal = detail::has_grant_of<detail::is_internal_grant, Grants...>();
        const bool has_bg = detail::has_grant_of<detail::is_bg_effect_grant, Grants...>();
        const bool has_declassify = detail::has_declassify_for_axis<detail::DischargeAxis::Bg, Grants...>();
        return has_internal && has_bg && !has_declassify;
    }

    static constexpr std::string_view name() noexcept { return "internal_bg_without_declassify"; }

    static constexpr std::string_view cite() noexcept {
        return "Bell-LaPadula 1973 / Smith-Volpano 1998 / "
               "Sabelfeld-Myers 2003 — concurrent no-write-down "
               "for Internal tier: org-internal value crosses into "
               "a background-thread context whose scheduling "
               "becomes Internal-tier-dependent.  Sequential IFC "
               "type systems are UNSOUND under concurrency (the "
               "spawn itself is a scheduler-observable event); "
               "Internal data is below the strict default "
               "(Classified) but ABOVE Public — every non-Public "
               "crossing through a scheduler-observable channel "
               "requires audit-trail discharge.  Insert "
               "grant::declassify<Policy> with a named cross-"
               "thread-authorization policy OR drop the Bg effect "
               "(run on the foreground thread where scheduling is "
               "deterministic) OR project Security to as_public / "
               "as_unclassified.";
    }

    static constexpr std::string_view full_diagnostic() noexcept {
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<&name, &cite>;
    }
};

struct external_to_verified_without_attest {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_external = detail::has_grant_of<detail::is_external_source_grant, Grants...>();
        const bool has_verified = detail::has_grant_of<detail::is_trust_verified_grant, Grants...>();
        const bool has_attest = detail::has_grant_of<detail::is_trust_assumed_grant, Grants...>();
        return has_external && has_verified && !has_attest;
    }

    static constexpr std::string_view name() noexcept { return "external_to_verified_without_attest"; }

    static constexpr std::string_view cite() noexcept {
        return "Biba 1977 'Integrity Considerations for Secure "
               "Computer Systems' MITRE MTR-3153 / Clark-Wilson 1987 "
               "'A Comparison of Commercial and Military Computer "
               "Security Policies' IEEE SP — integrity dual to BLP "
               "no-write-down: low-integrity source (External) flows "
               "into a high-integrity sink (trust_verified) without "
               "a documented Transformation Procedure attestation.  "
               "Insert grant::trust_assumed<Rationale> with a literal "
               "rationale documenting the integrity bridge, OR drop "
               "from_source<External> (input is already Sanitized), "
               "OR drop trust_verified (output retains Unverified).";
    }

    static constexpr std::string_view full_diagnostic() noexcept {
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<&name, &cite>;
    }
};

struct secret_unbounded_termination_channel {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_secret =
            detail::has_grant_of<detail::is_secret_grant, Grants...>() || detail::is_secret_type<Type>::value;
        const bool has_unbounded = detail::has_grant_of<detail::is_cost_unbounded_grant, Grants...>();
        const bool has_termination_discharge =
            detail::has_declassify_for_axis<detail::DischargeAxis::Termination, Grants...>();
        return has_secret && has_unbounded && !has_termination_discharge;
    }

    static constexpr std::string_view name() noexcept { return "secret_unbounded_termination_channel"; }

    static constexpr std::string_view cite() noexcept {
        return "Askarov-Hunt-Sabelfeld-Sands 2008 'Termination-"
               "Insensitive Noninterference Leaks More Than Just a "
               "Bit' (ESORICS) — PRIMARY: formalizes the covert "
               "termination channel.  Under termination-insensitive "
               "noninterference, an observer of repeated executions "
               "can extract arbitrarily many bits of a Secret by "
               "encoding it in the program's termination decisions; "
               "the classical TINI rationale that 'non-termination "
               "is unobservable' fails because real-world observers "
               "DO see whether processes complete.  Pattern: as_"
               "secret + cost_unbounded + no declassify policy whose "
               "axes_discharged_of covers DischargeAxis::Termination "
               "(no such policy currently ships — Termination is "
               "reserved per Hunt-Sands safe-default discipline).  "
               "Supporting: Volpano-Smith 1997 'A Type-Based "
               "Approach to Program Security' (CSFW) — extends the "
               "secure-flow type system to termination, motivating "
               "the bounded-Complexity proof obligation for "
               "Secret-dependent computations.  Remediation: drop "
               "grant::cost_unbounded (claim bounded Complexity, "
               "i.e. the function provably terminates on all "
               "inputs), OR drop the Secret engagement (project to "
               "as_public / as_unclassified), OR (future) interpose "
               "declassify<Policy> where Policy's axes_discharged_of "
               "lifts DischargeAxis::Termination.";
    }

    static constexpr std::string_view full_diagnostic() noexcept {
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<&name, &cite>;
    }
};

struct secret_catastrophic_staleness {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool has_secret =
            detail::has_grant_of<detail::is_secret_grant, Grants...>() || detail::is_secret_type<Type>::value;
        const bool has_catastrophic_stale = detail::any_stale_to_at_least<detail::kStaleToCatastrophic, Grants...>();
        const bool has_catastrophic_discharge =
            detail::has_declassify_for_axis<detail::DischargeAxis::CatastrophicReplay, Grants...>();
        return has_secret && has_catastrophic_stale && !has_catastrophic_discharge;
    }

    static constexpr std::string_view name() noexcept { return "secret_catastrophic_staleness"; }

    static constexpr std::string_view cite() noexcept {
        return "Askarov-Hunt-Sabelfeld-Sands 2008 'Termination-"
               "Insensitive Noninterference Leaks More Than Just a "
               "Bit' (ESORICS) — PRIMARY: the replay channel "
               "formalization is structurally equivalent to the "
               "termination channel.  An observer of repeated "
               "executions extracts arbitrarily many bits of a "
               "Secret by exploiting the replay surface, and the "
               "extraction rate scales with the replay window "
               "magnitude.  A catastrophic window (N >= 1024 "
               "generations of stale replay) carries qualitatively "
               "different attack-amortization risk than a mild "
               "window (N < 1024) and is held to a stricter "
               "discharge policy.  Supporting: Sabelfeld-Sands "
               "2009 'Declassification: Dimensions and Principles' "
               "(J. Computer Security) — the 'when' dimension "
               "(temporal authorization) admits a graded policy "
               "structure; AuthorizedReplay authorizes mild "
               "windows (any N via Staleness axis), but a "
               "catastrophic window requires a stronger policy "
               "(DischargeAxis::CatastrophicReplay, reserved here, "
               "no shipping policy lifts it yet).  Remediation: (a) "
               "lower stale_to<N> to N < 1024 (route through entry "
               "4's normal AuthorizedReplay discharge), (b) drop "
               "the Secret engagement (note: a Secret-wrapped "
               "payload still fires "
               "secret_payload_without_security_claim), "
               "OR (c) future declassify<Policy> with "
               "axes_discharged_of covering CatastrophicReplay.";
    }

    static constexpr std::string_view full_diagnostic() noexcept {
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<&name, &cite>;
    }
};

struct secret_payload_without_security_claim {
    template <typename Type, typename... Grants>
    [[nodiscard]] static consteval bool matches() noexcept {
        const bool type_is_secret = detail::is_secret_type<Type>::value;
        const bool has_security_engagement = detail::has_grant_of<detail::is_secret_grant, Grants...>();
        return type_is_secret && !has_security_engagement;
    }

    static constexpr std::string_view name() noexcept { return "secret_payload_without_security_claim"; }

    static constexpr std::string_view cite() noexcept {
        return "Sabelfeld-Sands 2009 'Declassification: Dimensions "
               "and Principles' (J. Computer Security) — PRIMARY: "
               "names the 'what' dimension of declassification and "
               "argues classification policies MUST be statically "
               "declared at the type level, not inferred from "
               "runtime payload introspection.  A safety::Secret<T> "
               "payload without a corresponding Security-axis grant "
               "is a silent type-level / wrapper-level divergence: "
               "the value type encodes classification ('this is "
               "Secret') while the binding's policy pack stays "
               "silent on Security.  Supporting: Sabelfeld-Myers "
               "2003 'Language-based Information-flow Security' "
               "(IEEE J. Sel. Areas) — information-flow policies "
               "are a TYPE-LEVEL artifact; classification through "
               "wrapper-only encoding is not statically checkable "
               "and admits silent policy drift.  Remediation: (a) "
               "engage Security explicitly via grant::as_secret or "
               "grant::as_classified (declare the classification at "
               "the policy level), (b) unwrap the Secret<T> via "
               "grant::declassify<Policy> BEFORE the binding "
               "boundary (the payload type reflects declassified "
               "data).  Note: downward Security projection via "
               "grant::as_public / as_unclassified is NOT a valid "
               "silencing remediation — `is_secret_grant` (the "
               "Security-engagement detector consumed here) "
               "intentionally matches only secrecy-raising or "
               "declassify-related forms, so adding as_public to a "
               "Secret<T>-wrapped payload still fires this entry as "
               "raw declassification (the formal audit channel is "
               "declassify<Policy>, not silent downward projection).";
    }

    static constexpr std::string_view full_diagnostic() noexcept {
        return ::crucible::fixy::theory::detail::kCorpusFullDiagnostic<&name, &cite>;
    }
};

}  // namespace corpus

namespace detail {

using CorpusEntries =
    std::tuple<corpus::classified_io_without_declassify, corpus::classified_bg_without_declassify,
               corpus::staleness_secret_without_declassify, corpus::ghost_runtime_observable,
               corpus::internal_io_without_declassify, corpus::internal_bg_without_declassify,
               corpus::external_to_verified_without_attest, corpus::secret_unbounded_termination_channel,
               corpus::secret_payload_without_security_claim, corpus::secret_catastrophic_staleness>;

template <typename Type, typename... Grants, typename Extractor>
[[nodiscard]] consteval std::string_view corpus_first_match_string_(Extractor extractor) noexcept {
    std::string_view result{};
    bool found = false;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        auto check = [&]<typename E>() consteval {
            if (!found && E::template matches<Type, Grants...>()) {
                result = extractor.template operator()<E>();
                found = true;
            }
        };
        (check.template operator()<std::tuple_element_t<Is, CorpusEntries>>(), ...);
    }(std::make_index_sequence<std::tuple_size_v<CorpusEntries>>{});
    return result;
}

}  // namespace detail

template <typename Type, typename... Grants>
[[nodiscard]] consteval bool is_in_unsoundness_corpus() noexcept {
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        return (std::tuple_element_t<Is, detail::CorpusEntries>::template matches<Type, Grants...>() || ...);
    }(std::make_index_sequence<std::tuple_size_v<detail::CorpusEntries>>{});
}

template <typename Type, typename... Grants>
inline constexpr bool IsInUnsoundnessCorpus_v = is_in_unsoundness_corpus<Type, Grants...>();

inline constexpr std::size_t corpus_size_v = 10;

namespace detail::corpus_size_sentinel {

struct entry_witness {
    std::string_view name;
    std::string_view cite;
    std::string_view full;
};

// The size of this array comes from the initializer count, not from a
// declared bound.  A C-style array with a bound would value-initialize
// a missing row in silence and make the drift check below tautological.
inline constexpr std::array kRoster = {
    entry_witness{corpus::classified_io_without_declassify::name(), corpus::classified_io_without_declassify::cite(),
                  corpus::classified_io_without_declassify::full_diagnostic()},
    entry_witness{corpus::classified_bg_without_declassify::name(), corpus::classified_bg_without_declassify::cite(),
                  corpus::classified_bg_without_declassify::full_diagnostic()},
    entry_witness{corpus::staleness_secret_without_declassify::name(),
                  corpus::staleness_secret_without_declassify::cite(),
                  corpus::staleness_secret_without_declassify::full_diagnostic()},
    entry_witness{corpus::ghost_runtime_observable::name(), corpus::ghost_runtime_observable::cite(),
                  corpus::ghost_runtime_observable::full_diagnostic()},
    entry_witness{corpus::internal_io_without_declassify::name(), corpus::internal_io_without_declassify::cite(),
                  corpus::internal_io_without_declassify::full_diagnostic()},
    entry_witness{corpus::internal_bg_without_declassify::name(), corpus::internal_bg_without_declassify::cite(),
                  corpus::internal_bg_without_declassify::full_diagnostic()},
    entry_witness{corpus::external_to_verified_without_attest::name(),
                  corpus::external_to_verified_without_attest::cite(),
                  corpus::external_to_verified_without_attest::full_diagnostic()},
    entry_witness{corpus::secret_unbounded_termination_channel::name(),
                  corpus::secret_unbounded_termination_channel::cite(),
                  corpus::secret_unbounded_termination_channel::full_diagnostic()},
    entry_witness{corpus::secret_payload_without_security_claim::name(),
                  corpus::secret_payload_without_security_claim::cite(),
                  corpus::secret_payload_without_security_claim::full_diagnostic()},
    entry_witness{corpus::secret_catastrophic_staleness::name(), corpus::secret_catastrophic_staleness::cite(),
                  corpus::secret_catastrophic_staleness::full_diagnostic()},
};
static_assert(kRoster.size() == corpus_size_v, "corpus_size_v no longer matches the roster below.  Adding an "
                                               "entry means appending a row here and bumping the count.");

static_assert(
    [] consteval {
        for (auto const& w : kRoster) {
            if (w.name.empty() || w.cite.empty()) {
                return false;
            }
        }
        return true;
    }(),
    "A corpus entry surfaced an empty name or cite.  Both carry the "
    "text of the rejection message, so neither may be empty.");

}  // namespace detail::corpus_size_sentinel

template <typename Type, typename... Grants>
concept NotInTheoryCorpus = !IsInUnsoundnessCorpus_v<Type, Grants...>;

// The three variables below return an empty view when no entry
// matches.  A caller never sees that case, because a binding with no
// match satisfies NotInTheoryCorpus and is never rejected.
template <typename Type, typename... Grants>
inline constexpr std::string_view corpus_cite_for_v = detail::corpus_first_match_string_<Type, Grants...>(
    []<typename E>() consteval -> std::string_view { return E::cite(); });

template <typename Type, typename... Grants>
inline constexpr std::string_view corpus_entry_name_for_v = detail::corpus_first_match_string_<Type, Grants...>(
    []<typename E>() consteval -> std::string_view { return E::name(); });

template <typename Type, typename... Grants>
inline constexpr std::string_view corpus_full_diagnostic_v = detail::corpus_first_match_string_<Type, Grants...>(
    []<typename E>() consteval -> std::string_view { return E::full_diagnostic(); });

}  // namespace crucible::fixy::theory
