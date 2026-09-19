#pragma once

// The atom catalog of fixy.  An atom is what a binding names to say
// more than the strict pole of one axis: `affine` relaxes Usage,
// `with<IO>` relaxes Effect, `declassify<Policy>` relaxes Security.
// Every atom is an empty final type that derives `atom_base` and
// carries the axis it engages as `static constexpr Axis axis`.  That
// member replaces the `which_dim` partial specialisation of the old
// tree: the axis is one line on the atom itself, so no header reopens
// a namespace to register it and no table of witnesses has to agree
// with it.
//
// An atom-less axis resolves to the strict pole in fixy/Axis.h.  That
// table is the only default.  There is no marker that accepts a
// default explicitly and no list of the axes that ship no atom.
//
// Old spelling: include/crucible/fixy/Grant.h (grant_base, IsGrantTag,
// which_dim, the tags of eighteen axes).  A grant is renamed an atom
// throughout, because a grant is an effect atom.  The families that
// reach the operating system live in fixy/atoms/Os.h and the
// control-flow, call-shape, stack, global-state and stdio families in
// fixy/atoms/{Ctrl,Dispatch,Stack,Global,Stdio}.h.

#include <fixy/Axis.h>
#include <fixy/Tags.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::atom {

struct atom_base {
    constexpr atom_base() noexcept = default;
    constexpr atom_base(const atom_base&) noexcept = default;
    constexpr atom_base(atom_base&&) noexcept = default;
    constexpr atom_base& operator=(const atom_base&) noexcept = default;
    constexpr atom_base& operator=(atom_base&&) noexcept = default;
    ~atom_base() = default;
};

// The one base every atom derives.  The axis is a data member of the
// base, so an atom is a one-line declaration and a reader sees its
// axis where the atom is declared.
template <Axis A>
struct atom_of : atom_base {
    static constexpr Axis axis = A;
};

// An atom that reaches a real operation also carries the effect row of
// that operation, so that a gate can lift an atom pack into the row the
// binding's context has to admit.  foundation/effects/Lift.h reads
// `lifts_to` structurally and never names this base.
template <Axis A, ::foundation::effects::IsEffectRow LiftRow>
struct lifting_atom_of : atom_of<A> {
    static constexpr LiftRow lifts_to{};
};

// The `final` clause is what stops a user from extending an already-shipped
// atom and injecting behavior into the acceptance check through the
// subclass.
//
// The cv-ref clause rejects rather than strips.  An atom is a zero-state
// phantom marker, so no legitimate path produces a cv-qualified or
// reference-qualified one; such a type comes from a `decltype` taken on a
// runtime variable (`const auto g = affine{};`) or on a reference return.
// Stripping with `std::remove_cvref_t` would coerce that mistake into the
// bare atom and accept it silently.
//
// What this gate cannot do is bound the set of atoms.  The recipe —
// `final`, derives `atom_base`, names an axis — is reproducible by anyone
// in any namespace, because the axis is a member and not a registration.
// The concept has no type-system handle on namespace identity, so a
// foreign type built that way is indistinguishable from a shipped atom.
// Closing that gap is a review and CI matter, not a type-system one.

template <class G>
concept IsAtom =
    std::same_as<G, std::remove_cvref_t<G>> && std::is_final_v<G> && std::derived_from<G, atom_base> && requires {
        { G::axis } -> std::convertible_to<Axis>;
    };

// Each concept below is the minimum structural bar a parametric atom's
// parameter must clear.  A parameter that fails one makes the atom
// template-id ill-formed, so `IsAtom` rejects the atom by substitution
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

// This is what makes the audit trail structural rather than a
// convention: an ad-hoc struct is rejected, so every declassification
// must name a tag from the closed set in fixy/Tags.h.  The tags there
// are final, and the clause here repeats that, so a subclass of a
// shipped policy cannot launder the trail through subtype coercion.
template <typename Policy>
concept IsDeclassificationPolicy =
    std::is_class_v<Policy> && std::derived_from<Policy, ::fixy::tags::secret_policy::secret_policy_base>
    && std::is_final_v<Policy>;

// A relaxation atom carries no meaning of its own beyond the axis it engages.
// The resolver that reads an atom pack decides what each atom resolves to;
// this header supplies only the axis the engagement check needs.

struct affine final : atom_of<Axis::Usage> {};
struct copy final : atom_of<Axis::Usage> {};
struct ghost final : atom_of<Axis::Usage> {};
struct borrow final : atom_of<Axis::Usage> {};
// The other four Usage atoms are bare nouns.  This one takes a suffix because
// the effect-axis capability token is spelled `Capability` and reaches most
// call sites that pull this header.  A bare `capability` here would still
// compile, and would read at those sites as the effect token.
struct capability_usage final : atom_of<Axis::Usage> {};

// An empty pack means the same thing as the strict pole for this axis:
// both resolve to the empty effect row.  An audit for pure-effect bindings
// has to recognise both spellings.

template <::foundation::effects::Effect... Es>
struct with final : atom_of<Axis::Effect> {};

using with_alloc = with<::foundation::effects::Effect::Alloc>;
using with_io = with<::foundation::effects::Effect::IO>;
using with_block = with<::foundation::effects::Effect::Block>;
using with_bg = with<::foundation::effects::Effect::Bg>;
using with_init = with<::foundation::effects::Effect::Init>;
using with_test = with<::foundation::effects::Effect::Test>;

// `declassify<Policy>` drops the binding to the public security level and
// names the policy that licenses the drop.  The policy is opaque to the
// engagement check and exists for the audit trail.

template <typename Policy>
    requires IsDeclassificationPolicy<Policy>
struct declassify final : atom_of<Axis::Security> {};

// One atom per point of the security lattice, ordered unclassified, public,
// internal, classified, secret.  `as_public` differs from `declassify<P>` in
// carrying no policy: it asserts the data was never classified rather than
// licensing a drop.  `as_classified` names the strict pole explicitly.
// `as_secret` pins the top of the lattice, where no declassification is
// permitted at all.
struct as_unclassified final : atom_of<Axis::Security> {};
struct as_public final : atom_of<Axis::Security> {};
struct as_internal final : atom_of<Axis::Security> {};
struct as_classified final : atom_of<Axis::Security> {};
struct as_secret final : atom_of<Axis::Security> {};

template <typename Proto>
    requires IsSessionProtocol<Proto>
struct protocol final : atom_of<Axis::Protocol> {};

template <auto RegionTag>
struct in_region final : atom_of<Axis::Lifetime> {};

template <typename Source>
    requires IsProvenanceSource<Source>
struct from_source final : atom_of<Axis::Provenance> {};

// The rationale is a non-type parameter, typically a character-array
// literal holding the human-readable justification.  The engagement check
// treats it opaquely, so it exists for the audit trail alone.  It has no
// default: a default would make "I gave no rationale" indistinguishable
// from a deliberate choice of that value, which is the one thing an audit
// of these sites needs to tell apart.
template <auto Rationale>
struct trust_assumed final : atom_of<Axis::Trust> {};

// One atom per remaining trust level.  `trust_verified` names the level the
// old default asserted; the strict pole in fixy/Axis.h is Unverified, so a
// binding names this atom only after it has discharged the proof.
// `trust_external` delegates the claim to a foreign source such as a vendor
// library or firmware, and a binding that takes it has to confirm that
// claim through some channel of its own.
struct trust_verified final : atom_of<Axis::Trust> {};
struct trust_tested final : atom_of<Axis::Trust> {};
struct trust_unverified final : atom_of<Axis::Trust> {};
struct trust_external final : atom_of<Axis::Trust> {};

template <::fixy::pole::ReprKind Kind>
struct repr final : atom_of<Axis::Representation> {};

// The Observability axis has no relaxation atom at all, and its absence is
// deliberate.  That axis is derived from the effect row, so an atom that let a
// binding state an observability of its own would contradict the derivation.

struct cost_constant final : atom_of<Axis::Complexity> {};
template <auto N>
struct cost_linear final : atom_of<Axis::Complexity> {};
template <auto N>
struct cost_quadratic final : atom_of<Axis::Complexity> {};
struct cost_unbounded final : atom_of<Axis::Complexity> {};

struct precision_f32 final : atom_of<Axis::Precision> {};
struct precision_f64 final : atom_of<Axis::Precision> {};
template <auto Bound>
struct precision_higham final : atom_of<Axis::Precision> {};

template <auto N>
struct space_bounded final : atom_of<Axis::Space> {};
struct space_unbounded final : atom_of<Axis::Space> {};

struct overflow_wrap final : atom_of<Axis::Overflow> {};
struct overflow_saturate final : atom_of<Axis::Overflow> {};
struct overflow_widen final : atom_of<Axis::Overflow> {};

struct mut_mutable final : atom_of<Axis::Mutation> {};
struct mut_append final : atom_of<Axis::Mutation> {};
struct mut_monotonic final : atom_of<Axis::Mutation> {};

// Unqualified, `coroutine` reads as a control-flow claim about suspending,
// which it is not: it engages the Reentrancy axis.  The control-flow atoms
// sit in a namespace of their own, so a reader who sees only the bare noun
// cannot tell the two apart.
struct reentrant final : atom_of<Axis::Reentrancy> {};
struct coroutine final : atom_of<Axis::Reentrancy> {};

// These names exist so a call site can spell the axis it engages.  The
// top-level spellings above stay valid, so this is the preferred form rather
// than the only one.
namespace reentrancy {
using reentrant = ::fixy::atom::reentrant;
using coroutine = ::fixy::atom::coroutine;
}  // namespace reentrancy

static_assert(std::is_same_v<reentrancy::coroutine, coroutine>,
              "atom::reentrancy::coroutine must alias the top-level "
              "atom::coroutine — the sub-namespace is a re-export, not a "
              "separate type.");
static_assert(std::is_same_v<reentrancy::reentrant, reentrant>, "atom::reentrancy::reentrant must alias the top-level "
                                                                "atom::reentrant — the sub-namespace is a re-export.");

template <auto Depth>
struct sized_at final : atom_of<Axis::Size> {};
struct productive final : atom_of<Axis::Size> {};

template <std::uint32_t V>
struct version final : atom_of<Axis::Version> {};

template <auto TauMax>
struct stale_to final : atom_of<Axis::Staleness> {};

template <typename Pred>
    requires IsRefinementPredicate<Pred>
struct refined_with final : atom_of<Axis::Refinement> {};

// The Type axis has no atom.  The bound type is the binding's own first
// template parameter, so no call site writes one.

// ── The roster walk ─────────────────────────────────────────────────
//
// A family's self-test is one hand list, the roster: a std::tuple of
// the atoms the family ships, with one instantiation per parametric
// atom.  Everything else derives from the roster by reflection.  The
// walk below checks each member once, so a family adds an atom by
// adding it to its roster and writes no per-atom assertion.

namespace detail {

// The members of a roster, as reflections of its template arguments.
// The roster is dealiased first, because the reflection of an alias
// has no template arguments of its own, and a joined roster is an
// alias.
template <class Roster>
inline constexpr auto roster_members_v =
    std::define_static_array(std::meta::template_arguments_of(std::meta::dealias(^^Roster)));

// Reads the enum, so an axis value that is not an enumerator fails
// here without a hand list of the enumerators.
[[nodiscard]] consteval bool is_axis_enumerator_(Axis value) noexcept {
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : axes) {
        if (value == [:en:]) return true;
    }
#pragma GCC diagnostic pop
    return false;
}

// Every member of the roster is an atom, is empty and one byte, names
// an axis that is an enumerator, and, if it lifts to a row, lifts to
// an effect row.
template <class Roster>
[[nodiscard]] consteval bool every_roster_member_is_atom_() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : roster_members_v<Roster>) {
        using A = [:member:];
        if constexpr (!IsAtom<A>) {
            return false;
        } else {
            if constexpr (sizeof(A) != 1 || !std::is_empty_v<A>) return false;
            if (!is_axis_enumerator_(A::axis)) return false;
            if constexpr (::foundation::effects::LiftsToRow<A>) {
                if constexpr (!::foundation::effects::IsEffectRow<decltype(A::lifts_to)>) return false;
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// Every member of the roster engages the one axis the family serves.
template <class Roster, Axis Expected>
[[nodiscard]] consteval bool every_roster_member_on_axis_() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : roster_members_v<Roster>) {
        using A = [:member:];
        if constexpr (!IsAtom<A>) {
            return false;
        } else {
            if (A::axis != Expected) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// Every member of the roster lifts to an effect row.
template <class Roster>
[[nodiscard]] consteval bool every_roster_member_lifts_() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : roster_members_v<Roster>) {
        using A = [:member:];
        if constexpr (!::foundation::effects::LiftsToRow<A>) {
            return false;
        } else {
            if constexpr (!::foundation::effects::IsEffectRow<decltype(A::lifts_to)>) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// Joins rosters, so a sentinel TU can walk every family as one list.
template <class... Rosters>
struct roster_cat;

template <>
struct roster_cat<> {
    using type = std::tuple<>;
};

template <class... As>
struct roster_cat<std::tuple<As...>> {
    using type = std::tuple<As...>;
};

template <class... As, class... Bs, class... Rest>
struct roster_cat<std::tuple<As...>, std::tuple<Bs...>, Rest...> {
    using type = typename roster_cat<std::tuple<As..., Bs...>, Rest...>::type;
};

template <class... Rosters>
using roster_cat_t = typename roster_cat<Rosters...>::type;

// Every atom class declared directly in Ns appears in the roster.  A
// roster is a hand list, and a check that walks the list cannot see
// what the list omits, so this reads the family namespace instead: an
// atom added there and forgotten in the roster is caught here rather
// than going unchecked.
//
// A class in the namespace that is not an atom is skipped.  The family
// namespaces hold policy tags beside their atoms, and a tag is an
// argument to an atom rather than an atom itself.
//
// Only the plain atom classes are checked.  A parametric atom reaches
// the roster as an instantiation, and deciding whether a class template
// is an atom template would mean instantiating it: there is no way to
// ask from the declaration alone.  That probe is not safe here.
// can_substitute on a template whose body holds a static_assert hard
// errors the translation unit rather than answering false, so a family
// that later grew such an atom would break every build instead of
// failing one check.  A parametric atom left out of a roster therefore
// stays uncaught, and the count pin beside each roster is what narrows
// that.
template <std::meta::info Ns, class Roster>
[[nodiscard]] consteval bool every_atom_in_is_rostered_() noexcept {
    static_assert(std::meta::is_namespace(Ns), "fixy/Atom.h: every_atom_in_is_rostered_<Ns, Roster> takes a "
                                               "reflection of a namespace, written ^^name.");
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(Ns, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                      && std::meta::is_class_type(member)) {
            using A = [:member:];
            if constexpr (IsAtom<A>) {
                bool is_rostered = false;
                template for (constexpr auto listed : roster_members_v<Roster>) {
                    // The splice is named before the comparison: one in
                    // a template-argument position needs a
                    // disambiguator, and an alias sidesteps that.
                    using Listed = [:listed:];
                    if constexpr (std::is_same_v<A, Listed>) is_rostered = true;
                }
                if (!is_rostered) return false;
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// Protocol, Lifetime and Provenance atoms are parameterized by a caller tag
// type, and their concepts require a COMPLETE empty class — an elaborated
// `struct X` written inline forward-declares instead, which the concept
// rejects.  Two local witnesses, defined here rather than borrowed from a
// production tag tree so the roster does not couple to one.
// `in_region` takes a non-type `auto` parameter, so it witnesses with a
// value rather than one of these tags.
struct atom_axis_witness_proto final {};
struct atom_axis_witness_source final {};

// The roster of this header: every atom above, one instantiation per
// parametric atom.
using core_atom_roster =
    std::tuple<affine, copy, ghost, borrow, capability_usage, with<>, with<::foundation::effects::Effect::IO>,
               with<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::IO>,
               declassify<::fixy::tags::secret_policy::AuditedLogging>, as_unclassified, as_public, as_internal,
               as_classified, as_secret, protocol<atom_axis_witness_proto>, protocol<::fixy::pole::proto::None>,
               in_region<0>, from_source<atom_axis_witness_source>, from_source<::fixy::tags::source::FromUser>,
               from_source<::fixy::tags::source::ForgePhase<'F'>>, trust_assumed<0>, trust_verified, trust_tested,
               trust_unverified, trust_external, repr<::fixy::pole::ReprKind::C>, cost_constant, cost_linear<1>,
               cost_quadratic<1>, cost_unbounded, precision_f32, precision_f64, precision_higham<1>, space_bounded<1>,
               space_unbounded, overflow_wrap, overflow_saturate, overflow_widen, mut_mutable, mut_append,
               mut_monotonic, reentrant, coroutine, sized_at<1>, productive, version<3>, stale_to<5>,
               refined_with<::fixy::pole::pred::True>>;

}  // namespace detail

namespace detail::atom_self_test {

static_assert(every_roster_member_is_atom_<core_atom_roster>(),
              "fixy/Atom.h: a member of core_atom_roster is not an atom, is not one empty byte, or names "
              "an axis that is not a fixy::Axis enumerator.");

static_assert(IsRefinementPredicate<::fixy::pole::pred::True>,
              "A named empty default-constructible predicate must satisfy "
              "IsRefinementPredicate.");

namespace found_037_witness {
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
}  // namespace found_037_witness

// A forge-phase source carries its phase as a non-type parameter and stays
// empty, so the empty-marker gate admits every instantiation.  A refactor
// that gave it state reds here rather than at the use sites.  The twelve
// phases A thru L are the range fixy/Tags.h pins on ForgePhase.
template <char... Offsets>
[[nodiscard]] consteval bool every_forge_phase_is_a_source_(std::integer_sequence<char, Offsets...>) noexcept {
    return (IsProvenanceSource<::fixy::tags::source::ForgePhase<static_cast<char>('A' + Offsets)>> && ...);
}
static_assert(every_forge_phase_is_a_source_(std::make_integer_sequence<char, 'L' - 'A' + 1>{}));

// The policy gate admits only the closed set in fixy/Tags.h.
struct ad_hoc_policy final {};
struct open_policy : ::fixy::tags::secret_policy::secret_policy_base {};
static_assert(IsDeclassificationPolicy<::fixy::tags::secret_policy::AuditedLogging>);
static_assert(IsDeclassificationPolicy<::fixy::tags::secret_policy::AuthorizedReplay>);
static_assert(!IsDeclassificationPolicy<ad_hoc_policy>, "A policy outside the closed set must be rejected.");
static_assert(!IsDeclassificationPolicy<open_policy>, "A policy that is not final must be rejected.");

// The gate rejects a cv-qualified or reference-qualified atom rather than
// stripping it, and rejects the two halves of the recipe on their own.
struct not_final : atom_of<Axis::Usage> {};
struct no_axis final : atom_base {};

static_assert(!IsAtom<const affine>);
static_assert(!IsAtom<volatile affine>);
static_assert(!IsAtom<const volatile affine>);
static_assert(!IsAtom<affine&>);
static_assert(!IsAtom<const affine&>);
static_assert(!IsAtom<affine&&>);
static_assert(!IsAtom<not_final>);
static_assert(!IsAtom<no_axis>);
static_assert(!IsAtom<atom_base>);
static_assert(!IsAtom<int>);

}  // namespace detail::atom_self_test

}  // namespace fixy::atom
