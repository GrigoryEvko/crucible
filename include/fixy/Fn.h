#pragma once

// A binding and what it promises on each of the 33 axes.
//
// `fn<T, Atoms...>` carries a value of type T and, for every axis, a
// grade.  An atom in the pack sets the grade on its axis.  An axis the
// pack does not mention takes its strict pole, silently.  That last
// word is the whole design: `fn<int>` is a legal binding meaning
// "strictest on all 33 axes", and every atom the author writes relaxes
// exactly one axis away from strict.
//
// The old tree made the opposite choice.  It had an engagement tier
// that demanded an `accept_default_strict_for<Axis>` marker on every
// axis the pack did not mention, so the shortest honest binding named
// 32 axes it had nothing to say about.  With 33 axes that is not a
// discipline, it is a tax, and the markers carried no information
// because they could not say anything but "default".  They are gone,
// along with Profile.h, sketch mode and CRUCIBLE_FIXY_STRICT.
//
// The resolver is one fold rather than 45 specialisations.  Asking for
// the grade on an axis walks the pack once and takes the entry whose
// axis matches, or the axis's strict pole when none does.  Adding an
// axis to the enum needs no edit here, and adding an atom needs no edit
// anywhere but the atom's own declaration.
//
// Old spelling: include/crucible/fixy/Fn.h, include/crucible/safety/Fn.h.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Reject.h>
#include <foundation/Platform.h>

#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

namespace fixy {

// Declared before the recognisers below, because the role gate names an
// fn specialization and the class body names the role gate.
template <class Type, class... Atoms>
class fn;

namespace detail::ctad {

// Deducing to this rather than failing outright is deliberate.  With no
// guide at all, `fixy::fn{42}` stops at "no viable deduction guide",
// which names no remedy.  Routing the deduction here reaches the tier-0
// assertion, which names the mint factories.
struct fn_ctad_blocked_use_mint_fn final {};

}  // namespace detail::ctad

// ---------------------------------------------------------------------
// Recognising a binding, and recognising a role.

template <class>
struct is_fn : std::false_type {};

template <class T, class... Atoms>
struct is_fn<fn<T, Atoms...>> : std::true_type {};

template <class F>
inline constexpr bool is_fn_v = is_fn<std::remove_cvref_t<F>>::value;

namespace detail::role {

template <class F>
struct accepted : std::false_type {};

template <class T, class... Atoms>
struct accepted<fn<T, Atoms...>> : std::bool_constant<IsAccepted<T, Atoms...>> {};

}  // namespace detail::role

// A role is an alias template naming one canonical pack, written in
// fixy/Role.h (A11.3).  The gate asks that the alias lands on an fn the
// gate itself admits, so a role cannot smuggle a pack past the tiers.
template <template <class> class Role, class T>
concept IsRoleFor = is_fn_v<Role<T>> && detail::role::accepted<Role<T>>::value;

namespace detail::resolve {

// An expansion statement redeclares its variable once per expansion and
// each declaration shadows the one before.  The warning is right about
// the shape and wrong about the risk.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// The silent default is only honest while every axis is classified.
//
// Twelve axes take axis_traits' defaulted primary, whose pole is
// pole::Unconstrained<A>.  For those twelve that IS the strict pole:
// they are wrapper-only, the claim rides on the value rather than on
// the binding, and a binding that says nothing about them is making no
// claim rather than granting one.  What makes that safe is the roster
// in Axis.h: an axis reaches the primary only by being named there.
//
// An axis added to the enum and classified neither way would reach the
// primary anyway and collect Unconstrained without anyone granting it —
// a silent free pass on a new axis, in every binding in the tree.
// Axis.h refuses that, and this assertion is where the resolver says it
// depends on the refusal, because this is the file that would hand the
// pass out.
static_assert(::fixy::every_axis_has_traits(),
              "fixy::fn resolves an unmentioned axis to axis_traits<A>::strict, so every axis "
              "must be classified: either it carries a hand-written specialisation, or it is "
              "named on Axis.h's defaulted_axes roster as wrapper-only.");

// The grade an axis takes when the pack says nothing about it.  Every
// axis but one has a strict pole.  Type is the exception: there is no
// default function type, so its grade is the payload the caller named.
template <Axis A, class T>
[[nodiscard]] consteval std::meta::info default_grade_() noexcept {
    // A value outside the enum reaches axis_traits' primary and would
    // come back Unconstrained, which reads as a granted claim on an
    // axis that does not exist.  Only a cast produces such a value, and
    // a cast into this resolver is a mistake rather than a question.
    static_assert(std::to_underlying(A) < ::fixy::axis_count,
                  "fixy::fn::grade_on<A>: A is not an Axis enumerator.  A value outside the enum "
                  "reaches the defaulted axis_traits primary and would resolve to an unconstrained "
                  "pole, granting a claim on an axis that does not exist.");
    if constexpr (requires { axis_traits<A>::caller_supplied; }) {
        return ^^T;
    } else {
        using Strict = typename axis_traits<A>::strict;
        return ^^Strict;
    }
}

// The one fold.  The pack is walked once and the entry whose axis
// matches wins; nothing matching leaves the default in place.  Tier 4
// has already refused a pack with two atoms on one axis, so "the last
// match wins" and "the only match wins" are the same answer here.
template <Axis A, class T, class... Atoms>
[[nodiscard]] consteval std::meta::info grade_() noexcept {
    std::meta::info found = default_grade_<A, T>();
    static constexpr auto entries = ::fixy::detail::reject::pack_entries_<Atoms...>();
    template for (constexpr auto entry : entries) {
        using Candidate = [:entry:];
        if constexpr (Candidate::axis == A) {
            found = entry;
        }
    }
    return found;
}

#pragma GCC diagnostic pop

}  // namespace detail::resolve

template <class Type, class... Atoms>
class fn {
    // Tier 0 comes first because the compiler processes the class body
    // in order, so this message reaches the reader before any later
    // tier can fire on the sentinel type.
    static constexpr bool tier0_not_ctad_sentinel_ = !std::is_same_v<Type, detail::ctad::fn_ctad_blocked_use_mint_fn>;
    static_assert(tier0_not_ctad_sentinel_,
                  "fixy::fn<Type, Atoms...> [tier 0]: class template argument deduction "
                  "(`fixy::fn{value}`) is not supported.  Every value-carrying fn is born "
                  "through a mint factory, so one grep for \"mint_\" finds every binding.  "
                  "Use fixy::mint_fn<Type, Atoms...>(value), or fixy::mint_fn_for<Role>(value) "
                  "for a role from fixy/Role.h.");

    // The tier that refuses this pack, computed once.  Exactly one
    // tier's assertion below fires, because the walk returns the FIRST
    // failure and stops: asking a later tier's question about a pack an
    // earlier one refused is not merely redundant, it is a hard error
    // from inside the walks.  fixy/Reject.h carries that reasoning.
    static constexpr detail::reject::Tier refused_at_ = detail::reject::first_failing_tier_<Type, Atoms...>();

    static constexpr bool tier1_payload_ok_ = refused_at_ != detail::reject::Tier::Payload;
    static_assert(tier1_payload_ok_, "fixy::fn<Type, Atoms...> [tier 1]: Type must be a non-array, "
                                     "non-reference, non-function, cv-unqualified object type, because "
                                     "the binding holds it by value.  fixy::unholdable_payload<Type> "
                                     "names the offending type.");

    static constexpr bool tier2_atoms_ok_ = refused_at_ != detail::reject::Tier::Malformed;
    static_assert(tier2_atoms_ok_, "fixy::fn<Type, Atoms...> [tier 2]: every entry in the pack must be an "
                                   "atom — a final class deriving fixy::atom::atom_of<Axis>.  "
                                   "fixy::malformed_atom<Offender> names the offending entry.");

    // There is no tier 3.  An unmentioned axis is not an error.

    // The message names the axis the pack graded twice, by name.
    static constexpr bool tier4_unique_ok_ = refused_at_ != detail::reject::Tier::Duplicate;
    static_assert(tier4_unique_ok_, detail::reject::tier4_message_<Atoms...>());

    // The message names the corpus entry that refused the pack with its
    // citation, the collision rules that refused it by code, or both.
    static constexpr bool tier5_composition_ok_ = refused_at_ != detail::reject::Tier::Composition;
    static_assert(tier5_composition_ok_, detail::reject::tier5_message_<Type, Atoms...>());

    // Naming the tag puts its class name into the compiler's
    // instantiation trail, beside the message above.  A passing pack
    // selects void, which names nothing and stays silent.  One alias
    // rather than one per tier, because only one tier ever fires.
    using refused_tag_ = [:detail::reject::tier_tag_<Type, Atoms...>():];

    // A payload tier 1 refused is replaced by a stand-in below, because
    // a member's type is instantiated with the class whatever the
    // assertion above concluded: `const void&` and a `void` parameter
    // are hard errors that buried the tier-1 message under six more.
    // For every payload tier 1 admits, held_ IS Type, so nothing about
    // a well-formed binding changes.
    using held_ = std::conditional_t<IsAcceptedPayload<Type>, Type, int>;

public:
    using value_type = Type;

    // The grade on one axis.  This is the whole resolver surface: there
    // are no resolve_usage_t, resolve_effect_t and seventeen siblings,
    // because every axis is asked the same question the same way.
    template <Axis A>
    using grade_on = [:detail::resolve::grade_<A, Type, Atoms...>():];

    // Whether the pack says anything about an axis.  A reader asking
    // "is this binding deliberate here, or did it take the default?"
    // gets an answer, which the silent-default design would otherwise
    // hide.
    template <Axis A>
    static constexpr bool mentions_axis = (detail::reject::count_on_axis_<A, Atoms...>() > 0);

    static constexpr std::size_t atom_count = sizeof...(Atoms);

    // The wrapper does not override Type's copy and move semantics,
    // even at a linear usage grade.  The grade records how the binding
    // is meant to be consumed, not how its storage behaves, so a
    // fn<int> stays copyable because int is.  Code that wants the
    // runtime guarantee wraps a move-only payload, and the defaulted
    // copy then disappears on its own.  Deriving the wrapper's copy
    // semantics from the grade instead would tie one axis of the
    // discipline to the structural shape of the value.
    //
    // The default constructor stays public so an fn can be a member of
    // an aggregate that default-initializes.  It carries no authority
    // only because the accessors below cannot write: a default binding
    // holds Type{} and can never hold anything else.
    constexpr fn() = default;

    // The accessors read and consume; none of them hands out a mutable
    // lvalue reference.  The old tree published one deducing-this
    // `auto&& value()`, which returns Type& for a non-const lvalue, and
    // that reopened the door the private constructor closes: with a
    // public default constructor, `fn<T, atoms...>{}.value() = x` wraps
    // any value under any pack without naming a mint.  Consuming
    // through the rvalue overload still moves the payload out, which is
    // the only mutation a binding's own discipline permits.
    [[nodiscard]] constexpr const held_& value() const& noexcept { return value_; }
    [[nodiscard]] constexpr held_&& value() && noexcept { return std::move(value_); }

private:
    // Wrapping a value is an authorization event: the caller asserts
    // that this value belongs under this pack.  Keeping the value
    // constructor private and befriending only the mint factories
    // leaves one name to grep for to find every such event.
    explicit constexpr fn(held_ v) noexcept(std::is_nothrow_move_constructible_v<held_>) : value_{std::move(v)} {}

    template <class T, class... G>
        requires IsAccepted<T, G...>
    friend constexpr auto mint_fn(T) noexcept(std::is_nothrow_move_constructible_v<T>) -> fn<T, G...>;

    template <template <class> class Role, class T>
        requires IsRoleFor<Role, T>
    friend constexpr auto mint_fn_for(T) noexcept(std::is_nothrow_move_constructible_v<T>) -> Role<T>;

    held_ value_{};
};

template <class T>
fn(T) -> fn<detail::ctad::fn_ctad_blocked_use_mint_fn>;

// ---------------------------------------------------------------------
// The doors.  The requires-clause repeats what the class body asserts.
// It earns its place by putting the gate in the signature, where a
// reader of the declaration sees it and where a failed call reports at
// the call site rather than inside the instantiation.

template <class Type, class... Atoms>
    requires IsAccepted<Type, Atoms...>
[[nodiscard]] constexpr auto mint_fn(Type v) noexcept(std::is_nothrow_move_constructible_v<Type>)
    -> fn<Type, Atoms...> {
    return fn<Type, Atoms...>{std::move(v)};
}

template <template <class> class Role, class Type>
    requires IsRoleFor<Role, Type>
[[nodiscard]] constexpr auto mint_fn_for(Type v) noexcept(std::is_nothrow_move_constructible_v<Type>) -> Role<Type> {
    return Role<Type>{std::move(v)};
}

// TODO(A6.2): the row-hash contribution.  Folding the 33 resolved
// grades into a federation cache key needs foundation/reflect/RowHash.h,
// which is blocked because the fold changes published keys.  The old
// tree folded only the axes at or after Synchronization, so whatever
// lands here will change the key regardless; that decision belongs with
// A6.2 and not here.

// ---------------------------------------------------------------------
// The header proves its own claims here.

namespace detail::fn_self_test {

// The shortest honest binding.  It names no axis and means strictest on
// every one of them.
static_assert(sizeof(fn<int>) == sizeof(int), "the wrapper adds no storage");
static_assert(alignof(fn<int>) == alignof(int));
static_assert(std::is_trivially_copyable_v<fn<int>>);
static_assert(fn<int>::atom_count == 0);

// The payload is its own grade on the Type axis, because no default
// function type exists.
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Type>, int>);
static_assert(std::is_same_v<fn<double>::grade_on<Axis::Type>, double>);

// An unmentioned axis takes its strict pole.  A dozen axes, drawn from
// each shape the table carries, rather than one.
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Usage>, typename axis_traits<Axis::Usage>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Effect>, typename axis_traits<Axis::Effect>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Refinement>, typename axis_traits<Axis::Refinement>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Security>, typename axis_traits<Axis::Security>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Mutation>, typename axis_traits<Axis::Mutation>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Reentrancy>, typename axis_traits<Axis::Reentrancy>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Regime>, typename axis_traits<Axis::Regime>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::MemoryScope>, typename axis_traits<Axis::MemoryScope>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::SimdIsa>, typename axis_traits<Axis::SimdIsa>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Stdio>, typename axis_traits<Axis::Stdio>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Version>, typename axis_traits<Axis::Version>::strict>);
static_assert(std::is_same_v<fn<int>::grade_on<Axis::Staleness>, typename axis_traits<Axis::Staleness>::strict>);

// Every axis resolves, for the empty pack and for a pack that mentions
// one axis.  A sample of twelve proves twelve; this proves the table.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
template <class Binding>
[[nodiscard]] consteval bool every_axis_resolves() noexcept {
    bool all_resolved = true;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^Axis))) {
        constexpr Axis axis = [:axis_member:];
        all_resolved = all_resolved && !std::is_void_v<typename Binding::template grade_on<axis>>;
    }
    return all_resolved;
}
#pragma GCC diagnostic pop

static_assert(every_axis_resolves<fn<int>>());
static_assert(every_axis_resolves<fn<int, atom::copy>>());

// One atom relaxes exactly its own axis and leaves the rest strict.
using CopyBinding = fn<int, atom::copy>;
static_assert(std::is_same_v<CopyBinding::grade_on<Axis::Usage>, atom::copy>);
static_assert(!std::is_same_v<CopyBinding::grade_on<Axis::Usage>, typename axis_traits<Axis::Usage>::strict>);
static_assert(std::is_same_v<CopyBinding::grade_on<Axis::Effect>, typename axis_traits<Axis::Effect>::strict>);
static_assert(std::is_same_v<CopyBinding::grade_on<Axis::Type>, int>);
static_assert(CopyBinding::mentions_axis<Axis::Usage>);
static_assert(!CopyBinding::mentions_axis<Axis::Effect>);
static_assert(CopyBinding::atom_count == 1);

// Atoms on different axes do not interfere.
using MixedBinding = fn<int, atom::affine, atom::mut_append, atom::reentrant>;
static_assert(std::is_same_v<MixedBinding::grade_on<Axis::Usage>, atom::affine>);
static_assert(std::is_same_v<MixedBinding::grade_on<Axis::Mutation>, atom::mut_append>);
static_assert(std::is_same_v<MixedBinding::grade_on<Axis::Reentrancy>, atom::reentrant>);
static_assert(std::is_same_v<MixedBinding::grade_on<Axis::Security>, typename axis_traits<Axis::Security>::strict>);
static_assert(MixedBinding::atom_count == 3);

// The gate refuses what it should, at the concept rather than only in
// the class body, so a mint call reports at the call site.
struct not_an_atom {};
static_assert(IsAccepted<int>);
static_assert(IsAccepted<int, atom::copy>);
static_assert(IsAccepted<int, atom::copy, atom::mut_append>);
static_assert(!IsAccepted<int, atom::copy, atom::affine>, "two atoms on the Usage axis");
static_assert(!IsAccepted<int, not_an_atom>);
static_assert(!IsAccepted<void, atom::copy>);
static_assert(!IsAccepted<int&>);

// The duplicate diagnostic names the axis rather than the atoms, which
// is what a reader needs to fix it.
static_assert(std::is_same_v<duplicate_tag_or_void_t<atom::copy, atom::affine>, duplicate_atom_on<Axis::Usage>>);
static_assert(duplicate_tag_or_void_t<atom::copy, atom::affine>::name == "DuplicateAtomOnUsage");
static_assert(std::is_same_v<duplicate_tag_or_void_t<atom::copy, atom::mut_append>, void>);

// The door carries the value through and the type records the pack.
[[nodiscard]] consteval bool mint_carries_the_value() noexcept {
    const auto bound = mint_fn<int, atom::copy>(42);
    return bound.value() == 42;
}
static_assert(mint_carries_the_value());
static_assert(std::is_same_v<decltype(mint_fn<int, atom::copy>(0)), fn<int, atom::copy>>);

// The value constructor is not a public door.  A caller can still
// default-construct, which carries no authority, but cannot wrap a
// value without naming a mint.
static_assert(!std::is_constructible_v<fn<int, atom::copy>, int>,
              "the value constructor is private; mint_fn is the only door");
static_assert(std::is_default_constructible_v<fn<int, atom::copy>>);

static_assert(is_fn_v<fn<int>>);
static_assert(is_fn_v<const fn<int, atom::copy>&>);
static_assert(!is_fn_v<int>);

}  // namespace detail::fn_self_test

}  // namespace fixy
