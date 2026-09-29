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
// There is no engagement tier.  A marker that accepts the default on
// each axis the pack does not mention would make the shortest honest
// binding name 32 axes it has nothing to say about.  With 33 axes that
// is not a discipline, it is a tax, and such a marker carries no
// information, because it cannot say anything but "default".
//
// The resolver is one fold and not 45 specializations, and it is
// fixy::collision::grades in fixy/Collision.h, which the collision rules
// read too.  A request for the grade on an axis walks the pack one time
// and takes the entry whose axis agrees, or the strict pole of the axis
// when no entry agrees.  An axis added to the enum takes no edit here,
// and an atom takes no edit other than its own declaration.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Reject.h>
#include <fixy/Tags.h>
#include <fixy/atoms/Syscall.h>
#include <foundation/Platform.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/reflect/Hash.h>

#include <cstddef>
#include <cstdint>
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
struct is_accepted_fn : std::false_type {};

template <class T, class... Atoms>
struct is_accepted_fn<fn<T, Atoms...>> : std::bool_constant<IsAccepted<T, Atoms...>> {};

}  // namespace detail::role

// A role is an alias template naming one canonical pack, written in
// fixy/Role.h.  The gate asks that the alias lands on an fn the
// gate itself admits, so a role cannot smuggle a pack past the tiers.
template <template <class> class Role, class T>
concept IsRoleFor = is_fn_v<Role<T>> && detail::role::is_accepted_fn<Role<T>>::value;

namespace detail::resolve {

// The grade on one axis.  The Type axis has no pole, so its grade is the
// payload the binding names.  Every other axis resolves through
// fixy::collision::grades, the resolver that the collision rules and the
// corpus read, so a binding and its rules cannot resolve one pack two
// ways.  That resolver also holds the assertion that every axis is
// classified, and it refuses a value outside the enum.
template <Axis A, class T, class... Atoms>
[[nodiscard]] consteval std::meta::info grade_() noexcept {
    if constexpr (IsCallerSupplied<A>) {
        return ^^T;
    } else {
        using Grade = typename ::fixy::collision::grades<Atoms...>::template on<A>;
        return ^^Grade;
    }
}

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

    // The message names the first entry that is not an atom and, for an
    // entry with the shape of an atom, the read that refuses it.
    static constexpr bool tier2_atoms_ok_ = refused_at_ != detail::reject::Tier::Malformed;
    static_assert(tier2_atoms_ok_, detail::reject::tier2_message_<Atoms...>());

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
    // lvalue reference.  An accessor that returns Type& for a non-const
    // lvalue would reopen the door the private constructor closes: with
    // a public default constructor, `fn<T, atoms...>{}.value() = x` would
    // wrap any value under any pack without naming a mint.  Consuming
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

// ---------------------------------------------------------------------
// The row a binding requires of its caller's context.
//
// A binding declares its effects in two places: the grade on
// Axis::Effect, and the lift of each atom that reaches a real operation.
// A futex call, a park and a system call each lift the row of the
// operation they name.  The row a binding requires is the union of the
// two.  The concept below is the gate a caller writes, so a caller does
// not write the row a second time by hand.
//
// The work is in fixy/Atom.h.  Its closed relation answers for both
// shapes the grade can take, the stated `with<Es...>` and the bare Row
// the strict pole leaves behind, and binding_row_of_t joins that row
// with the lifts of the pack.  A grade of any other shape fails there,
// with its own name in the diagnostic.  The collision rules and the
// corpus read the same join, so the gate and the rules cannot disagree
// about what a binding does.
//
// Why the lifts are part of the row.  A binding can state a futex call,
// `fn<int, syscall::per<SyscallId::futex>>`, and leave its Effect grade
// at the strict pole.  A row read from the grade alone is then the empty
// row, and the empty row is a Subrow of the row of every context.  A
// context that admits nothing then admits a call that can park the
// caller.
//
// The federation key does not read this row.  It folds the grade on each
// axis, and a lift is not a grade, so a lift cannot move the key of a
// binding.
namespace detail::binding_row {

template <class F>
struct of_;

template <class Type, class... Atoms>
struct of_<fn<Type, Atoms...>> {
    using type = atom::binding_row_of_t<typename fn<Type, Atoms...>::template grade_on<Axis::Effect>, Atoms...>;
};

}  // namespace detail::binding_row

template <class F>
    requires is_fn_v<F>
using binding_row_t = typename detail::binding_row::of_<std::remove_cvref_t<F>>::type;

// A context admits a binding when it admits every effect the binding
// declared.  Both halves are checked: a first argument that is not a
// context fails here rather than being read as one.
template <class Ctx, class F>
concept CtxAdmitsBinding =
    ::foundation::effects::IsExecCtx<Ctx> && is_fn_v<F>
    && ::foundation::effects::CtxAdmits<Ctx, binding_row_t<F>>;

// ---------------------------------------------------------------------
// The federation cache key of a binding.
//
// A cache key pairs a content hash with a row hash.  The content hash
// says which computation; the row hash says under which discipline.  A
// binding is exactly a discipline over a payload, so the row hash is the
// only half of the key that can tell two bindings over one payload apart.
//
// Without a specialisation here a binding reaches the primary template in
// foundation/diag/RowHash.h, which answers zero.  Zero is the right
// answer for a payload carrying no row and the wrong answer for a
// binding: every binding in the tree would share one slot, and a kernel
// compiled for a pure copy would be served to a caller that performs IO
// and emits publicly.  That is a soundness failure rather than a cache
// inefficiency, because the two disciplines are not interchangeable.
//
// The fold cannot be the graded one.  That fold reads a single modality,
// a single lattice and a payload, which is what a one-axis carrier
// publishes; a binding publishes a grade per axis and is the resolver
// across them, so it has no singular lattice and no singular modality to
// offer.  It folds its own axes instead.
//
// The roster is derived from the Axis enum rather than listed, so an axis
// added to the table folds in without an edit here.  A listed roster can
// lose an axis, and an axis outside the list cannot reach the key at
// all.
//
// Three shapes of grade fold three ways.
//
// The Type axis carries the payload, which may itself be a row-bearing
// wrapper stack, so it recurses through the same contribution.  A bare
// payload contributes zero there and the remaining axes carry the
// discrimination, which is deliberate: payload identity belongs to the
// content hash, the other half of the key.
//
// The Effect axis folds through the row rather than through the grade's
// type, and that is what makes the key a function of the effect SET.  A
// row is a set of atoms, `with<Bg, Alloc>` and `with<Alloc, Bg>` are
// distinct types naming one set, and the row specialisation sorts and
// dedups.  Reading the grade's type instead would give those two spellings
// two slots for one discipline.  It also collapses the axis's two legal
// grade shapes onto one answer, the stated `with<Es...>` and the bare row
// its strict pole leaves behind, which is what fixy/Atom.h already
// promises about them: an empty stated pack and the strict pole mean the
// same thing.
//
// Every other axis folds the canonical identity of its resolved grade,
// which it reads through foundation/diag/RowHash.h's
// lattice_canonical_id.  That trait defaults to the reflected identity,
// and the reflected identity is the only canonical identity an atom
// publishes — an atom carries no meaning beyond the axis it engages, by
// fixy/Atom.h's own words.  It also discriminates a parametric atom by
// its argument, so a declassification under one policy takes a different
// slot from the same declassification under another.
//
// The indirection is what lets one axis hold two grade spellings for one
// claim.  An atom that names a level, and the strict pole at that same
// level, are distinct types.  A fold that reads the reflected identity
// directly therefore gives one claim two slots.  The Effect axis never
// had that problem, because the branch above folds through the row and
// the row already collapses a stated empty pack onto the strict pole.
// Every other axis closes it one specialisation at a time.
//
// Each specialisation is one judgment, and the judgment is made against
// the code rather than against the naming.  The direction of the two
// errors is not symmetric.  Two spellings left apart cost a cache miss
// and a recompile.  Two spellings merged without an equivalence behind
// them serve a kernel compiled under one discipline to a caller under
// another, which is the direction this key must never move.  So a
// specialisation is owed a consumer that already treats the two
// spellings alike, and the Security and Trust axes show that the naming
// alone does not tell: the two read the same way and decide opposite
// ways.  The cells at the foot of test/fixy/test_row_hash_wrappers.cpp
// hold both answers, with the consumer that establishes each.
//
// What does not fold in is whether the pack MENTIONED an axis.  Two
// bindings whose resolved grades agree make the same claim, and whether
// one wrote the atom out is a diagnostic distinction rather than an
// identity one.  Folding it would split one discipline across two slots.
//
// The combiner is order-sensitive, so the walk is in enumerator order and
// nothing sorts it.  That order is the enum's, which is append-only.  A new
// axis still moves every key: the walk folds one more step for it into
// every binding, the strict pole included, so each B and S entry of the
// foundation golden changes.  Appending keeps the old axes at their fold
// positions, so a renumbering is worse, but neither leaves a key in place.
// A new point on an existing axis moves only the keys of bindings that
// spell it.
//
// Portability is the bound foundation/diag/RowHash.h sets out. A grade's
// identity comes from a reflected name, so these keys agree only among
// peers on one toolchain, and peers that are not must key through
// federation_key_with_toolchain.

namespace detail::row_hash {

// An expansion statement redeclares its variable per expansion and each
// declaration shadows the one before, the same shape the resolver above
// suppresses for the same reason.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

template <class Binding>
[[nodiscard]] consteval std::uint64_t fold_axes_() noexcept {
    std::uint64_t h = ::foundation::diag::detail::WRAPPER_MULTI_AXIS_BINDING_TAG;
    // The range is materialised into a static array because the
    // reflection query returns a vector, whose allocation is not a
    // constant in the context an expansion statement needs.
    static constexpr auto axis_members = std::define_static_array(std::meta::enumerators_of(^^Axis));
    template for (constexpr auto axis_member : axis_members) {
        constexpr Axis axis = [:axis_member:];
        using Grade = typename Binding::template grade_on<axis>;
        if constexpr (axis == Axis::Type) {
            h = ::foundation::diag::detail::combine_ids(h, ::foundation::diag::row_hash_contribution_v<Grade>);
        } else if constexpr (axis == Axis::Effect) {
            h = ::foundation::diag::detail::combine_ids(
                h, ::foundation::diag::row_hash_contribution_v<atom::effect_row_of_t<Grade>>);
        } else {
            h = ::foundation::diag::detail::combine_ids(h, ::foundation::diag::lattice_canonical_id_v<Grade>);
        }
    }
    return h;
}

#pragma GCC diagnostic pop

}  // namespace detail::row_hash

}  // namespace fixy

// The specialisations live on this side of the layer boundary because
// foundation may name only foundation and the standard library, while
// fixy may name foundation.  A binding and an atom are both fixy types,
// so the only place the two layers can meet is here.
namespace foundation::diag {

// `as_classified` and the strict Security pole are one claim under two
// spellings, and this is what puts them in one cache slot.
//
// What establishes the equivalence is a consumer, not the naming.  Every
// corpus entry that reads the Security axis for classification reads it
// through fixy/Corpus.h's `is_classified_on_`, which answers true on
// every channel for the strict pole and for `as_classified` alike — "the
// strict pole counts, because a binding that says nothing about Security
// is classified", in that header's words.  No other predicate separates
// them: `is_internal_` answers false for both, and a mask reads
// `declassify` only.  The collision rules read the same closed relation
// of fixy/Atom.h, which gives both the class Classified.  So the two
// spellings get one verdict on every pack, and the cells in
// test/fixy/test_row_hash_wrappers.cpp assert that rather than cite it.
//
// fixy/Atom.h settles it a second way, definitionally: `as_classified`
// "names the strict pole explicitly".  The atom exists in order to write
// the default out, and the fold above already holds that writing an axis
// out is a diagnostic distinction rather than an identity one.
//
// `as_secret` is the third spelling the same predicate accepts, and it is
// deliberately NOT mapped here.  fixy/Atom.h gives it a residual claim
// the other two do not carry — it "pins the top of the lattice, where no
// declassification is permitted at all".  Two points of Conf make that
// claim coincide with the pole today, and tier 4 makes it unobservable,
// because an atom and a `declassify` cannot both sit on one axis.  A rule
// that makes the claim real would separate `as_secret` from the pole, and
// a merge shipped now would then be a wrong hit rather than a stale one.
// A later author who establishes that the residual claim is empty maps it
// here, one judgment, with the consumer named.
//
// The pole is reached through this same trait rather than through the
// reflected identity, so a canonicalisation of the pole carries the atom
// with it instead of splitting the pair again.
template <>
struct lattice_canonical_id<::fixy::atom::as_classified> {
    static constexpr std::uint64_t value =
        lattice_canonical_id_v<typename ::fixy::axis_traits<::fixy::Axis::Security>::strict>;
};

// `trust_unverified` and the strict Trust pole are one claim under two
// spellings, as `as_classified` and the strict Security pole are.  The
// consumer that establishes it is rule T001 in fixy/Collision.h, the one
// rule that reads the Trust axis.  It reads the grade through the closed
// relation `trust_class_of_`, which gives the two spellings the class
// Unverified.  The other Trust atoms keep their own slots, because each
// of them admits a capability that the pole refuses.
template <>
struct lattice_canonical_id<::fixy::atom::trust_unverified> {
    static constexpr std::uint64_t value =
        lattice_canonical_id_v<typename ::fixy::axis_traits<::fixy::Axis::Trust>::strict>;
};

template <class Type, class... Atoms>
struct row_hash_contribution<::fixy::fn<Type, Atoms...>> {
    static constexpr std::uint64_t value = ::fixy::detail::row_hash::fold_axes_<::fixy::fn<Type, Atoms...>>();
};

}  // namespace foundation::diag

namespace fixy {

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
[[nodiscard]] consteval bool door_carries_the_value() noexcept {
    const auto bound = mint_fn<int, atom::copy>(42);
    return bound.value() == 42;
}
static_assert(door_carries_the_value());
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

// ---------------------------------------------------------------------
// What the fold over the axes owes the federation cache.

namespace fd_ = ::foundation::diag;
namespace fe_ = ::foundation::effects;

// A binding is off the zero slot, which is where the primary template
// leaves anything it does not recognise.  A binding that landed there
// would share one slot with every bare type in the tree.
static_assert(fd_::row_hash_contribution_v<fn<int>> != 0);
static_assert(fd_::row_hash_contribution_v<int> == 0);
static_assert(fd_::row_hash_contribution_v<fn<int>> != fd_::row_hash_contribution_v<int>);

// The binding does not satisfy the graded fold's shape, and must not: it
// publishes a grade per axis rather than one lattice and one modality.
// This is the assertion that would red if someone gave it a synthetic
// product lattice to make the generic fold apply.
static_assert(!fd_::GradedShaped<fn<int>>);
static_assert(!fd_::GradedShaped<fn<int, atom::copy>>);

// One atom on one axis moves the key, and two grades on one axis take
// separate slots.  Between them these say the fold reads the resolved
// grade and not merely the pack's length.
static_assert(fd_::row_hash_contribution_v<fn<int, atom::copy>> != fd_::row_hash_contribution_v<fn<int>>);
static_assert(fd_::row_hash_contribution_v<fn<int, atom::affine>> != fd_::row_hash_contribution_v<fn<int, atom::copy>>);
static_assert(fd_::row_hash_contribution_v<fn<int, atom::affine, atom::mut_append>>
              != fd_::row_hash_contribution_v<fn<int, atom::affine>>);

// The Effect axis folds the row, so the key is a function of the effect
// SET.  These two spellings are distinct types naming one set, and one
// slot for both is the whole reason the axis does not fold its grade's
// type.
static_assert(
    fd_::row_hash_contribution_v<fn<int, atom::with<fe_::Effect::Bg, fe_::Effect::Alloc>, atom::as_public>>
        == fd_::row_hash_contribution_v<fn<int, atom::with<fe_::Effect::Alloc, fe_::Effect::Bg>, atom::as_public>>,
    "the Effect axis must fold as a set: two orderings of one row are one discipline and "
    "belong in one slot");

// A stated empty row and the strict pole it leaves behind are the same
// claim, which fixy/Atom.h says of them, so they take one slot.  This is
// also where the fold states that it ignores whether the pack MENTIONED
// an axis: one of these two mentions Effect and the other does not, and
// their resolved grades agree.
static_assert(fn<int, atom::with<>>::mentions_axis<Axis::Effect>);
static_assert(!fn<int>::mentions_axis<Axis::Effect>);
static_assert(fd_::row_hash_contribution_v<fn<int, atom::with<>>> == fd_::row_hash_contribution_v<fn<int>>,
              "an axis mentioned and an axis defaulted to the same grade are one discipline; "
              "mentioning must not split the slot");

// The key is blind to a bare payload, deliberately.  Payload identity
// belongs to the content hash, the other half of a cache key, and the
// Type axis recurses so that a payload which does carry a row reaches the
// key instead of being dropped.
static_assert(fd_::row_hash_contribution_v<fn<int>> == fd_::row_hash_contribution_v<fn<double>>,
              "the row hash discriminates disciplines, not payload types");

// A parametric atom is discriminated by its argument, so an emission
// under one policy does not reuse the slot of an emission under another.
static_assert(fd_::row_hash_contribution_v<fn<int, atom::declassify<tags::secret_policy::WireSerialize>>>
                  != fd_::row_hash_contribution_v<fn<int, atom::declassify<tags::secret_policy::AuditedLogging>>>,
              "a declassification's policy is part of its identity");

// The row a binding requires joins its Effect grade with the lifts of its
// atoms.  A futex call requires Block although the Effect grade stays at
// the strict pole.
using FutexBinding = fn<int, atom::syscall::per<atom::syscall::SyscallId::futex>>;
using StatedBlockBinding = fn<int, atom::with<fe_::Effect::Block>>;
static_assert(std::is_same_v<FutexBinding::grade_on<Axis::Effect>, typename axis_traits<Axis::Effect>::strict>);
static_assert(std::is_same_v<binding_row_t<FutexBinding>, fe_::Row<fe_::Effect::Block>>);
static_assert(std::is_same_v<binding_row_t<fn<int>>, fe_::Row<>>);

// The key folds the grade on each axis, and not the row a binding
// requires.  These two bindings require one row and state it on two
// different axes, so they take two slots.
static_assert(std::is_same_v<binding_row_t<FutexBinding>, binding_row_t<StatedBlockBinding>>);
static_assert(fd_::row_hash_contribution_v<FutexBinding> != fd_::row_hash_contribution_v<StatedBlockBinding>,
              "the key folds the axis grades, and the row of the binding is not one of them");

}  // namespace detail::fn_self_test

}  // namespace fixy
