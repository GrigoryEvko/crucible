#pragma once

#include <crucible/fixy/Dim.h>
#include <crucible/safety/NotInherited.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/Fn.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <array>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace crucible::fixy::grant {

struct grant_base {
    constexpr grant_base() noexcept = default;
    constexpr grant_base(const grant_base&) noexcept = default;
    constexpr grant_base(grant_base&&) noexcept = default;
    constexpr grant_base& operator=(const grant_base&) noexcept = default;
    constexpr grant_base& operator=(grant_base&&) noexcept = default;
    ~grant_base() = default;
};

// The `final` clause is what stops a user from extending an already-shipped
// grant tag and injecting behavior into the acceptance check through the
// subclass.
//
// The cv-ref clause rejects rather than strips.  A grant tag is a zero-state
// phantom marker, so no legitimate path produces a cv-qualified or
// reference-qualified one; such a type comes from a `decltype` taken on a
// runtime variable (`const auto g = affine{};`) or on a reference return.
// Stripping with `std::remove_cvref_t` would coerce that mistake into the
// bare tag and accept it silently.
//
// What this gate cannot do is bound the set of grant tags.  The recipe —
// `final`, derives `grant_base`, has a `which_dim` specialization — is
// reproducible by anyone who reopens this namespace, because an explicit
// specialization of `which_dim` must appear syntactically inside it and C++
// has no access control on that.  The concept has no type-system handle on
// namespace identity, so a foreign type registered that way is
// indistinguishable from a shipped tag.  Closing that gap is a review and CI
// matter, not a type-system one.

template <typename G>
inline constexpr bool IsGrantTag_v =
    std::is_same_v<G, std::remove_cvref_t<G>> && std::is_base_of_v<grant_base, G> && std::is_final_v<G>;

template <typename G>
concept IsGrantTag = IsGrantTag_v<G>;

// Each concept below is the minimum structural bar a parametric grant's
// parameter must clear.  A parameter that fails one makes the grant
// template-id ill-formed, so `IsGrantTag` rejects the tag by substitution
// failure instead of the build hitting a hard error inside the resolver.

// The parameter of `protocol<P>` is a session type or a machine state type,
// so this gate stays open-world.  Enumerating the legal session combinators
// instead would be tighter but would refuse any user-defined state class.
template <typename Proto>
concept IsSessionProtocol = std::is_same_v<Proto, std::remove_cvref_t<Proto>> && std::is_class_v<Proto>;

// Whether a predicate is invocable on a given value type cannot be folded
// into a per-predicate gate, so that check happens per-type at construction
// and is deliberately absent here.
//
// Emptiness and default-constructibility are required for a reason unrelated
// to calling the predicate.  A binding that engages `refined_with<Pred>`
// carries `Pred` into its cache key, and every distinct `Pred` type claims
// its own slot.  A stateful predicate struct, or a capturing lambda, has a
// fresh type per declaration site, so two textually identical ones fragment
// the cache into two slots.  Requiring empty and default-constructible
// forces bindings onto named predicates, which share a type across call
// sites.  A capture-less lambda is empty and default-constructible, so it
// still passes.
template <typename Pred>
concept IsRefinementPredicate = std::is_same_v<Pred, std::remove_cvref_t<Pred>> && std::is_class_v<Pred>
                             && std::is_empty_v<Pred> && std::is_default_constructible_v<Pred>;

// A provenance source is an empty marker class by convention, and this gate
// is stricter than the two above because that convention is stricter.  A
// source tag that starts carrying state surfaces here rather than downstream
// in resolution.
template <typename Source>
concept IsProvenanceSource =
    std::is_same_v<Source, std::remove_cvref_t<Source>> && std::is_class_v<Source> && std::is_empty_v<Source>;

// A tag with no specialization has no axis, and the error surfaces at the
// engagement check that reads `which_dim_v`, not here.

template <typename G>
struct which_dim;

template <typename G>
inline constexpr dim::DimensionAxis which_dim_v = which_dim<G>::value;

// ── Which axes ship no grant tag of their own ────────────────────────
//
// An axis listed here has no grant tag.  A binding on such an axis reaches
// `project<accept_default_strict_for<D>>` and can only accept the strict
// default, because no alternative stance is expressible.
//
// fix-36: this list used to live in Fn.h and name THIRTEEN axes, twelve of
// which had since grown a grant family — SyscallSurface alone has 33
// `which_dim` specializations, FpMode 12, HwInstruction 9.  Fn.h even
// included Hw.h and exercised `grant::hw::msr` while its own table called
// HwInstruction grantless.  The two assertions guarding the table pinned
// only its LENGTH against a hand-written literal equal to that length, so
// they compared a number to itself and could not see that the CONTENTS had
// gone false.  A list that names the wrong axes routes real grants into the
// strict-default path in any future consumer of `axis_has_grant_family`.
//
// The table now lives here, in Grant.h, because Grant.h is the one header
// every grant family already includes.  That lets each family assert, at
// the site where its `which_dim` specializations are written, that its own
// axis is NOT in the table — see `grant_family_witnessed_v` below.  The
// claim "axis D has zero which_dim specializations" is not directly
// expressible (C++ cannot enumerate the specializations of a template), so
// it is enforced from the other side: every axis that HAS a specialization
// carries a witness, and a witness contradicting the table is a hard error.
//
// Three axes are genuinely grantless, for two different reasons:
//   * Type and Observability are STRUCTURAL.  Type is the carrier type
//     itself and Observability is derived from the Effect row, so neither
//     is awaiting a grant family — neither will ever get one.
//   * Regime is AWAITING one.  When the first `with_regime<…>` tag ships,
//     its header adds a witness, the witness contradicts this table, and
//     the build goes red until Regime is removed from the list.
inline constexpr std::array kAxesWithoutNonDefaultGrants = {
    dim::DimensionAxis::Type,
    dim::DimensionAxis::Observability,
    dim::DimensionAxis::Regime,
};

namespace audit {

[[nodiscard]] constexpr bool axis_is_grantless(dim::DimensionAxis D) noexcept {
    for (auto gap_axis : kAxesWithoutNonDefaultGrants) {
        if (gap_axis == D) return true;
    }
    return false;
}

[[nodiscard]] constexpr bool axis_has_grant_family(dim::DimensionAxis D) noexcept { return !axis_is_grantless(D); }

// A grant tag witnesses its own axis.  Reading `which_dim_v<G>` is what
// makes this a real check rather than a restatement: the value comes from
// G's `which_dim` specialization, so the assertion fails exactly when a
// specialization exists for an axis the table calls grantless.
template <typename G>
inline constexpr bool grant_family_witnessed_v = axis_has_grant_family(which_dim_v<G>);

template <typename... Gs>
inline constexpr bool grant_families_witnessed_v = (grant_family_witnessed_v<Gs> && ...);

// Both operands are derived.  `kAxesWithoutNonDefaultGrants.size()` comes
// from the initializer list above (class template argument deduction — no
// hand-written extent to drift), and `DIMENSION_AXIS_COUNT` is computed by
// reflection over the enum.  Appending or removing an axis on either side
// moves one of the two numbers, so the pin fires.
static_assert(kAxesWithoutNonDefaultGrants.size() == 3,
              "Three DimensionAxis values ship no grant tag: Type and Observability "
              "structurally, Regime pending its first grant family.  Adding a fourth "
              "means a new axis landed without grants; removing one means a family "
              "shipped.  Either way, update this count in the same edit.");

static_assert(safety::DIMENSION_AXIS_COUNT - kAxesWithoutNonDefaultGrants.size() == 30,
              "Thirty of the 33 DimensionAxis values ship a grant family.  A fire "
              "means an axis was added or removed without reconciling "
              "kAxesWithoutNonDefaultGrants against it.");

}  // namespace audit

// This marker confers nothing.  It asserts only that the author read the
// discipline for axis D and chose its strict default, which the resolver
// then supplies.

template <dim::DimensionAxis D>
struct accept_default_strict_for final : grant_base {};

template <dim::DimensionAxis D>
struct which_dim<accept_default_strict_for<D>> : std::integral_constant<dim::DimensionAxis, D> {};

// A relaxation tag carries no meaning of its own beyond the axis it engages.
// The resolver that reads a grant pack decides what each tag resolves to;
// this header supplies only the axis mapping the engagement check needs.

struct affine final : grant_base {};
struct copy final : grant_base {};
struct ghost final : grant_base {};
struct borrow final : grant_base {};
// The other four Usage tags are bare nouns.  This one takes a suffix because
// the effect-axis capability token is spelled `Capability` and reaches most
// call sites that pull this header.  A bare `capability` here would still
// compile, and would read at those sites as the effect token.
struct capability_usage final : grant_base {};

template <>
struct which_dim<affine> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Usage> {};
template <>
struct which_dim<copy> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Usage> {};
template <>
struct which_dim<ghost> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Usage> {};
template <>
struct which_dim<borrow> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Usage> {};
template <>
struct which_dim<capability_usage> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Usage> {};

// An empty pack means the same thing as the acceptance marker for this axis:
// both resolve to the empty effect row.  An audit for pure-effect bindings
// has to recognise both spellings.

template <effects::Effect... Es>
struct with final : grant_base {};

template <effects::Effect... Es>
struct which_dim<with<Es...>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Effect> {};

using with_alloc = with<effects::Effect::Alloc>;
using with_io = with<effects::Effect::IO>;
using with_block = with<effects::Effect::Block>;
using with_bg = with<effects::Effect::Bg>;
using with_init = with<effects::Effect::Init>;
using with_test = with<effects::Effect::Test>;

// `declassify<Policy>` drops the binding to the public security level and
// names the policy that licenses the drop.  The policy is opaque to the
// engagement check and exists for the audit trail.

template <typename Policy>
    requires ::crucible::safety::DeclassificationPolicy<Policy>
struct declassify final : grant_base {};

template <typename Policy>
    requires ::crucible::safety::DeclassificationPolicy<Policy>
struct which_dim<declassify<Policy>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Security> {};

// One tag per point of the security lattice, ordered unclassified, public,
// internal, classified, secret.  `as_public` differs from `declassify<P>` in
// carrying no policy: it asserts the data was never classified rather than
// licensing a drop.  `as_classified` names the strict default explicitly.
// `as_secret` pins the top of the lattice, where no declassification is
// permitted at all.
struct as_unclassified final : grant_base {};
struct as_public final : grant_base {};
struct as_internal final : grant_base {};
struct as_classified final : grant_base {};
struct as_secret final : grant_base {};

template <>
struct which_dim<as_unclassified> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Security> {};
template <>
struct which_dim<as_public> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Security> {};
template <>
struct which_dim<as_internal> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Security> {};
template <>
struct which_dim<as_classified> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Security> {};
template <>
struct which_dim<as_secret> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Security> {};

template <typename Proto>
    requires IsSessionProtocol<Proto>
struct protocol final : grant_base {};

template <typename Proto>
    requires IsSessionProtocol<Proto>
struct which_dim<protocol<Proto>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Protocol> {};

template <auto RegionTag>
struct in_region final : grant_base {};

template <auto RegionTag>
struct which_dim<in_region<RegionTag>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Lifetime> {};

template <typename Source>
    requires IsProvenanceSource<Source>
struct from_source final : grant_base {};

template <typename Source>
    requires IsProvenanceSource<Source>
struct which_dim<from_source<Source>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Provenance> {};

// A type-trait query on `trust_assumed` still has to name a rationale it
// does not care about.  This is the value such a query passes, and a
// rationale spelled this way is by construction not a production claim.
inline constexpr int axis_query_tag = 0;

// The rationale is a non-type parameter, typically a character-array
// literal holding the human-readable justification.  The engagement check
// treats it opaquely, so it exists for the audit trail alone.  It has no
// default: a default would make "I gave no rationale" indistinguishable
// from a deliberate choice of that value, which is the one thing an audit
// of these sites needs to tell apart.
template <auto Rationale>
struct trust_assumed final : grant_base {};

template <auto Rationale>
struct which_dim<trust_assumed<Rationale>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Trust> {};

// One tag per remaining trust level.  `trust_verified` names the strict
// default explicitly.  `trust_external` delegates the claim to a foreign
// source such as a vendor library or firmware, and a binding that takes it
// has to confirm that claim through some channel of its own.
struct trust_verified final : grant_base {};
struct trust_tested final : grant_base {};
struct trust_unverified final : grant_base {};
struct trust_external final : grant_base {};

template <>
struct which_dim<trust_verified> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Trust> {};
template <>
struct which_dim<trust_tested> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Trust> {};
template <>
struct which_dim<trust_unverified> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Trust> {};
template <>
struct which_dim<trust_external> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Trust> {};

template <safety::fn::ReprKind Kind>
struct repr final : grant_base {};

template <safety::fn::ReprKind Kind>
struct which_dim<repr<Kind>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Representation> {};

// The Observability axis has no relaxation tag at all, and its absence is
// deliberate.  That axis is derived from the effect row, so a tag that let a
// binding state an observability of its own would contradict the derivation.
// Only the acceptance marker is legal there.

struct cost_constant final : grant_base {};
template <auto N>
struct cost_linear final : grant_base {};
template <auto N>
struct cost_quadratic final : grant_base {};
struct cost_unbounded final : grant_base {};

template <>
struct which_dim<cost_constant> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Complexity> {};
template <auto N>
struct which_dim<cost_linear<N>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Complexity> {};
template <auto N>
struct which_dim<cost_quadratic<N>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Complexity> {};
template <>
struct which_dim<cost_unbounded> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Complexity> {};

struct precision_f32 final : grant_base {};
struct precision_f64 final : grant_base {};
template <auto Bound>
struct precision_higham final : grant_base {};

template <>
struct which_dim<precision_f32> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Precision> {};
template <>
struct which_dim<precision_f64> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Precision> {};
template <auto Bound>
struct which_dim<precision_higham<Bound>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Precision> {
};

template <auto N>
struct space_bounded final : grant_base {};
struct space_unbounded final : grant_base {};

template <auto N>
struct which_dim<space_bounded<N>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Space> {};
template <>
struct which_dim<space_unbounded> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Space> {};

struct overflow_wrap final : grant_base {};
struct overflow_saturate final : grant_base {};
struct overflow_widen final : grant_base {};

template <>
struct which_dim<overflow_wrap> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Overflow> {};
template <>
struct which_dim<overflow_saturate> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Overflow> {};
template <>
struct which_dim<overflow_widen> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Overflow> {};

struct mut_mutable final : grant_base {};
struct mut_append final : grant_base {};
struct mut_monotonic final : grant_base {};

template <>
struct which_dim<mut_mutable> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Mutation> {};
template <>
struct which_dim<mut_append> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Mutation> {};
template <>
struct which_dim<mut_monotonic> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Mutation> {};

// Unqualified, `coroutine` reads as a control-flow claim about suspending,
// which it is not: it engages the Reentrancy axis.  The control-flow tags
// sit in a namespace of their own, so a reader who sees only the bare noun
// cannot tell the two apart.
struct reentrant final : grant_base {};
struct coroutine final : grant_base {};

template <>
struct which_dim<reentrant> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Reentrancy> {};
template <>
struct which_dim<coroutine> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Reentrancy> {};

// These names exist so a call site can spell the axis it engages.  The
// top-level spellings above stay valid, so this is the preferred form rather
// than the only one.
namespace reentrancy {
using reentrant = ::crucible::fixy::grant::reentrant;
using coroutine = ::crucible::fixy::grant::coroutine;
}  // namespace reentrancy

static_assert(std::is_same_v<reentrancy::coroutine, coroutine>,
              "grant::reentrancy::coroutine must alias the top-level "
              "grant::coroutine — the sub-namespace is a re-export, not a "
              "separate type.");
static_assert(std::is_same_v<reentrancy::reentrant, reentrant>, "grant::reentrancy::reentrant must alias the top-level "
                                                                "grant::reentrant — the sub-namespace is a re-export.");
static_assert(which_dim<reentrancy::coroutine>::value == dim::DimensionAxis::Reentrancy,
              "grant::reentrancy::coroutine must resolve to "
              "DimensionAxis::Reentrancy.");
static_assert(which_dim<reentrancy::reentrant>::value == dim::DimensionAxis::Reentrancy,
              "grant::reentrancy::reentrant must resolve to "
              "DimensionAxis::Reentrancy.");

template <auto Depth>
struct sized_at final : grant_base {};
struct productive final : grant_base {};

template <auto Depth>
struct which_dim<sized_at<Depth>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Size> {};
template <>
struct which_dim<productive> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Size> {};

template <std::uint32_t V>
struct version final : grant_base {};

template <std::uint32_t V>
struct which_dim<version<V>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Version> {};

template <auto TauMax>
struct stale_to final : grant_base {};

template <auto TauMax>
struct which_dim<stale_to<TauMax>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Staleness> {};

template <typename Pred>
    requires IsRefinementPredicate<Pred>
struct refined_with final : grant_base {};

template <typename Pred>
    requires IsRefinementPredicate<Pred>
struct which_dim<refined_with<Pred>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Refinement> {};

// The Type axis has no tag of either kind here.  The bound type is the
// binding's own first template parameter, and its engagement marker is
// synthesized when the binding is constructed, so no call site writes one.

namespace detail::grant_self_test {

static_assert(IsGrantTag<accept_default_strict_for<dim::DimensionAxis::Usage>>);
static_assert(IsGrantTag<affine>);
static_assert(IsGrantTag<copy>);
static_assert(IsGrantTag<ghost>);
static_assert(IsGrantTag<borrow>);
static_assert(IsGrantTag<capability_usage>);
static_assert(IsGrantTag<with<effects::Effect::IO>>);
static_assert(IsGrantTag<with<>>);
static_assert(IsGrantTag<declassify<::crucible::safety::secret_policy::AuditedLogging>>);
static_assert(IsGrantTag<as_unclassified>);
static_assert(IsGrantTag<as_public>);
static_assert(IsGrantTag<as_internal>);
static_assert(IsGrantTag<as_classified>);
static_assert(IsGrantTag<as_secret>);
static_assert(IsGrantTag<trust_verified>);
static_assert(IsGrantTag<trust_tested>);
static_assert(IsGrantTag<trust_unverified>);
static_assert(IsGrantTag<trust_external>);
static_assert(IsGrantTag<refined_with<safety::fn::pred::True>>);

static_assert(IsRefinementPredicate<safety::fn::pred::True>,
              "A named empty default-constructible predicate must satisfy "
              "IsRefinementPredicate.");

namespace detail::found_037_witness {
struct StatefulPredicate {
    int threshold = 0;
    [[nodiscard]] constexpr bool operator()(int v) const noexcept { return v > threshold; }
};
static_assert(!IsRefinementPredicate<StatefulPredicate>,
              "A stateful predicate must be rejected by IsRefinementPredicate "
              "— each unique stateful Pred type fragments the cache.");

struct NonDefaultConstructiblePredicate {
    constexpr NonDefaultConstructiblePredicate(int) noexcept {}
    [[nodiscard]] constexpr bool operator()(int v) const noexcept { return v > 0; }
};
static_assert(!IsRefinementPredicate<NonDefaultConstructiblePredicate>,
              "A non-default-constructible predicate must be rejected — the "
              "refinement machinery instantiates Pred freely.");

using CaptureLessLambdaType = decltype([](int v) noexcept { return v > 0; });
static_assert(std::is_empty_v<CaptureLessLambdaType>);
static_assert(std::is_default_constructible_v<CaptureLessLambdaType>);
static_assert(IsRefinementPredicate<CaptureLessLambdaType>,
              "A capture-less lambda is empty and default-constructible, so "
              "IsRefinementPredicate must admit it.");
}  // namespace detail::found_037_witness
static_assert(IsGrantTag<version<3>>);
static_assert(IsGrantTag<stale_to<5>>);

static_assert(sizeof(as_unclassified) == 1);
static_assert(sizeof(as_public) == 1);
static_assert(sizeof(as_internal) == 1);
static_assert(sizeof(as_classified) == 1);
static_assert(sizeof(as_secret) == 1);
static_assert(sizeof(trust_verified) == 1);
static_assert(sizeof(trust_tested) == 1);
static_assert(sizeof(trust_unverified) == 1);
static_assert(sizeof(trust_external) == 1);

static_assert(which_dim_v<as_unclassified> == dim::DimensionAxis::Security);
static_assert(which_dim_v<as_public> == dim::DimensionAxis::Security);
static_assert(which_dim_v<as_internal> == dim::DimensionAxis::Security);
static_assert(which_dim_v<as_classified> == dim::DimensionAxis::Security);
static_assert(which_dim_v<as_secret> == dim::DimensionAxis::Security);

static_assert(which_dim_v<trust_verified> == dim::DimensionAxis::Trust);
static_assert(which_dim_v<trust_tested> == dim::DimensionAxis::Trust);
static_assert(which_dim_v<trust_unverified> == dim::DimensionAxis::Trust);
static_assert(which_dim_v<trust_external> == dim::DimensionAxis::Trust);

static_assert(sizeof(affine) == 1);
static_assert(sizeof(copy) == 1);
static_assert(sizeof(ghost) == 1);
static_assert(sizeof(with<>) == 1);
static_assert(sizeof(with<effects::Effect::Bg, effects::Effect::IO>) == 1);
static_assert(sizeof(accept_default_strict_for<dim::DimensionAxis::Trust>) == 1);

static_assert(which_dim_v<affine> == dim::DimensionAxis::Usage);
static_assert(which_dim_v<copy> == dim::DimensionAxis::Usage);
static_assert(which_dim_v<with<effects::Effect::IO>> == dim::DimensionAxis::Effect);
static_assert(which_dim_v<declassify<::crucible::safety::secret_policy::AuditedLogging>>
              == dim::DimensionAxis::Security);
static_assert(which_dim_v<refined_with<safety::fn::pred::True>> == dim::DimensionAxis::Refinement);
static_assert(which_dim_v<accept_default_strict_for<dim::DimensionAxis::Trust>> == dim::DimensionAxis::Trust);
static_assert(which_dim_v<version<3>> == dim::DimensionAxis::Version);
static_assert(which_dim_v<stale_to<5>> == dim::DimensionAxis::Staleness);
static_assert(which_dim_v<repr<safety::fn::ReprKind::C>> == dim::DimensionAxis::Representation);
static_assert(which_dim_v<overflow_wrap> == dim::DimensionAxis::Overflow);

// ── Grant-family witnesses for the axes Grant.h itself serves ────────
//
// fix-36: one witness per axis whose grant tags are declared in this
// header.  Each reads the axis out of the tag's own `which_dim`
// specialization and checks it against `kAxesWithoutNonDefaultGrants`.
// Listing any of these eighteen axes as grantless is now a hard error
// instead of a silent lie, which is what let the old table drift for
// twelve axes at once.  The remaining twelve witnesses live beside the
// `which_dim` specializations they attest — four in Hw.h, and one each in
// Fp.h, Fs.h, Async.h, grant/Ctrl.h, grant/Dispatch.h, grant/Global.h,
// grant/Stack.h and grant/Stdio.h.
static_assert(
    audit::grant_families_witnessed_v<affine,  // Usage
                                      with<effects::Effect::IO>,  // Effect
                                      declassify<::crucible::safety::secret_policy::AuditedLogging>,  // Security
                                      trust_verified,  // Trust
                                      refined_with<safety::fn::pred::True>,  // Refinement
                                      version<3>,  // Version
                                      stale_to<5>,  // Staleness
                                      repr<safety::fn::ReprKind::C>,  // Representation
                                      overflow_wrap,  // Overflow
                                      reentrancy::reentrant>,  // Reentrancy
    "Ten axes served by Grant.h carry grant tags, so none of Usage, Effect, "
    "Security, Trust, Refinement, Version, Staleness, Representation, "
    "Overflow or Reentrancy may appear in kAxesWithoutNonDefaultGrants.");

// Protocol, Lifetime and Provenance tags are parameterized by a caller tag
// type, and their concepts require a COMPLETE empty class — an elaborated
// `struct X` written inline forward-declares instead, which the concept
// rejects.  Three local witnesses, defined here rather than borrowed from a
// production tag tree so the audit does not couple to one.
// `in_region` takes a non-type `auto` parameter, so it witnesses with a
// value rather than one of these tags.
struct grant_axis_witness_proto final {};
struct grant_axis_witness_source final {};

static_assert(audit::grant_families_witnessed_v<protocol<grant_axis_witness_proto>,  // Protocol
                                                in_region<0>,  // Lifetime
                                                from_source<grant_axis_witness_source>,  // Provenance
                                                cost_constant,  // Complexity
                                                precision_f32,  // Precision
                                                space_bounded<1>,  // Space
                                                mut_mutable,  // Mutation
                                                sized_at<1>>,  // Size
              "Eight further axes served by Grant.h carry grant tags, so none of "
              "Protocol, Lifetime, Provenance, Complexity, Precision, Space, Mutation "
              "or Size may appear in kAxesWithoutNonDefaultGrants.");
static_assert(which_dim_v<mut_mutable> == dim::DimensionAxis::Mutation);
static_assert(which_dim_v<reentrant> == dim::DimensionAxis::Reentrancy);
static_assert(which_dim_v<cost_constant> == dim::DimensionAxis::Complexity);
static_assert(which_dim_v<precision_f64> == dim::DimensionAxis::Precision);
static_assert(which_dim_v<space_unbounded> == dim::DimensionAxis::Space);
static_assert(which_dim_v<productive> == dim::DimensionAxis::Size);
static_assert(which_dim_v<in_region<0>> == dim::DimensionAxis::Lifetime);
static_assert(which_dim_v<from_source<safety::source::FromUser>> == dim::DimensionAxis::Provenance);
// A forge-phase source carries its phase as a non-type parameter and stays
// empty, so the empty-marker gate admits every instantiation.  A refactor
// that gave it state reds here rather than at the use sites.
static_assert(IsProvenanceSource<safety::source::ForgePhase<'A'>>);  // INGEST
static_assert(IsProvenanceSource<safety::source::ForgePhase<'B'>>);  // ANALYZE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'C'>>);  // REWRITE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'D'>>);  // FUSE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'E'>>);  // LOWER_TO_KERNELS
static_assert(IsProvenanceSource<safety::source::ForgePhase<'F'>>);  // TILE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'G'>>);  // MEMPLAN
static_assert(IsProvenanceSource<safety::source::ForgePhase<'H'>>);  // COMPILE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'I'>>);  // SCHEDULE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'J'>>);  // EMIT
static_assert(IsProvenanceSource<safety::source::ForgePhase<'K'>>);  // DISTRIBUTE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'L'>>);  // VALIDATE
static_assert(which_dim_v<from_source<safety::source::ForgePhase<'F'>>> == dim::DimensionAxis::Provenance);
static_assert(which_dim_v<trust_assumed<axis_query_tag>> == dim::DimensionAxis::Trust);
static_assert(which_dim_v<protocol<safety::fn::proto::None>> == dim::DimensionAxis::Protocol);

static_assert(IsGrantTag_v<affine>);
static_assert(IsGrantTag_v<copy>);
static_assert(IsGrantTag_v<as_public>);

static_assert(!IsGrantTag_v<const affine>);
static_assert(!IsGrantTag_v<volatile affine>);
static_assert(!IsGrantTag_v<const volatile affine>);

static_assert(!IsGrantTag_v<affine&>);
static_assert(!IsGrantTag_v<const affine&>);
static_assert(!IsGrantTag_v<affine&&>);

}  // namespace detail::grant_self_test

}  // namespace crucible::fixy::grant
