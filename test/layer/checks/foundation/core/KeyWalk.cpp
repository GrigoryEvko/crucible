// The compile-time checks of foundation/core/KeyWalk.h.

#include <foundation/core/KeyWalk.h>

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Id.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>

#include <cstddef>
#include <meta>
#include <utility>

namespace foundation::core {

namespace detail::key_walk_checks {

struct CheckBrand final {};
struct OtherBrand final {};
struct PlainBrand final {};
using Key = LoanKey<CheckBrand>;
using OtherKey = LoanKey<OtherBrand>;
using KeyedArray = FixedArray<int, 4, CheckBrand>;

template <class Root>
consteval bool reaches_key() {
    return reaches_loan_key_(^^Root, std::meta::dealias(^^Key));
}

template <class Root>
consteval bool borrows_owner() {
    return reaches_borrowed_key_owner_(^^Root, ^^CheckBrand);
}

// Each way to a key: a member, a base, a reference, a pointer, an array, a
// function type, a pointer to member, a type argument and the type of a value
// argument.  The control of each one names the key of another brand.
struct HoldsReference {
    Key& key;
};
struct HoldsOther {
    OtherKey& key;
};
struct DerivesFromHolder : HoldsReference {};
template <class T>
struct NamesType {};
template <auto Value>
struct NamesValue {};
static_assert(reaches_key<HoldsReference>() && reaches_key<DerivesFromHolder>() && !reaches_key<HoldsOther>());
static_assert(reaches_key<HoldsReference[3]>() && reaches_key<HoldsReference*>() && !reaches_key<HoldsOther*>());
static_assert(reaches_key<void (*)(Key&)>() && reaches_key<Key (*)()>() && !reaches_key<void (*)(OtherKey&)>());
static_assert(reaches_key<int HoldsReference::*>() && reaches_key<HoldsReference * NamesType<int>::*>());
static_assert(reaches_key<NamesType<Key>>() && reaches_key<NamesType<NamesType<HoldsReference*>>>());
static_assert(reaches_key<NamesValue<static_cast<Key*>(nullptr)>>());
static_assert(!reaches_key<NamesType<HoldsOther>>() && !reaches_key<NamesValue<static_cast<OtherKey*>(nullptr)>>());
static_assert(!reaches_key<NamesType<PlainBrand>>() && !reaches_key<NamesValue<7>>());
static_assert(reaches_key<Key>() && reaches_key<Key&>() && reaches_key<Key*>() && !reaches_key<OtherKey>());
static_assert(!reaches_key<int>() && !reaches_key<int*>() && !reaches_key<void (*)(int)>());

// A borrow and an owner give their type arguments as parts, and they hide their
// members.  The walk reads the hidden members too.
template <class T>
struct[[= ::foundation::transfer::refers{}]] BorrowWithHiddenKey {
    T& target;
    Key& hidden;
};
struct[[= ::foundation::transfer::refers{}]] PlainBorrowWithHiddenKey {
    Key& hidden;
};
static_assert(type_parts_v<BorrowWithHiddenKey<int>>.hides_members);
static_assert(reaches_key<BorrowWithHiddenKey<int>>() && reaches_key<PlainBorrowWithHiddenKey>());
static_assert(reaches_key<NamesType<BorrowWithHiddenKey<int>>>());
static_assert(!reaches_key<View<int>>() && !reaches_key<Box<int>>() && reaches_key<Box<HoldsReference>>());

// A closure answers with the union of its captures, at each depth, and so does
// a closure that holds a closure.
template <class Held>
constexpr auto capture_by_value(Held const& held) {
    return [held]() noexcept { static_cast<void>(held); };
}
template <class Held>
constexpr auto capture_by_reference(Held& held) {
    return [&held]() noexcept { static_cast<void>(held); };
}
template <class Held>
using ValueClosure = decltype(capture_by_value(std::declval<Held const&>()));
template <class Held>
using ReferenceClosure = decltype(capture_by_reference(std::declval<Held&>()));
static_assert(reaches_key<ValueClosure<HoldsReference>>() && reaches_key<ReferenceClosure<HoldsReference>>());
static_assert(reaches_key<ValueClosure<ValueClosure<HoldsReference>>>());
static_assert(reaches_key<ReferenceClosure<ValueClosure<HoldsReference>>>());
static_assert(reaches_key<ValueClosure<HoldsReference*>>() && reaches_key<ReferenceClosure<HoldsReference* const>>());
static_assert(!reaches_key<ValueClosure<int>>() && !reaches_key<ReferenceClosure<int>>());
static_assert(!reaches_key<ValueClosure<HoldsOther>>() && !reaches_key<ReferenceClosure<HoldsOther>>());
static_assert(!reaches_key<ValueClosure<HoldsOther*>>() && !reaches_key<ValueClosure<ValueClosure<HoldsOther>>>());

// Each capture counts, whatever the capture before it reaches: nothing, or
// another marked class.
template <class First, class Second>
constexpr auto capture_two(First const& first, Second& second) {
    return [first, &second]() noexcept {
        static_cast<void>(first);
        static_cast<void>(second);
    };
}
template <class First, class Second>
using TwoCaptures = decltype(capture_two(std::declval<First const&>(), std::declval<Second&>()));
static_assert(reaches_key<TwoCaptures<int, HoldsReference>>() && reaches_key<TwoCaptures<HoldsReference, int>>());
static_assert(reaches_key<TwoCaptures<HoldsOther, HoldsReference>>());
static_assert(reaches_key<TwoCaptures<HoldsReference, HoldsOther>>());
static_assert(!reaches_key<TwoCaptures<HoldsOther, int>>() && !reaches_key<TwoCaptures<int, HoldsOther>>());

// A class with no name has the shape of a closure.  A mark of a borrow on it
// hides no member of it, and an answer does not name it, so it is no key.
constexpr auto make_unnamed_borrow(Key& key) {
    struct[[= ::foundation::transfer::refers{}]] {
        Key& hidden;
    } unnamed{key};
    return unnamed;
}
constexpr auto make_unnamed(Key& key) {
    struct {
        Key& shown;
    } unnamed{key};
    return unnamed;
}
constexpr auto make_unnamed_bits() {
    struct {
        int : 7;
    } unnamed{};
    return unnamed;
}
constexpr auto make_unnamed_key() {
    struct[[= loan_key_mark{}]] {
    } unnamed{};
    return unnamed;
}
static_assert(reaches_key<decltype(make_unnamed_borrow(std::declval<Key&>()))>());
static_assert(reaches_key<decltype(make_unnamed(std::declval<Key&>()))>());
static_assert(reaches_key<decltype(make_unnamed_bits())>() && borrows_owner<decltype(make_unnamed_bits())>());
static_assert(reaches_loan_key_(^^int, std::meta::dealias(^^decltype(make_unnamed_key()))));

// A class with more parts than one row holds gives each part.
struct Wide {
    HoldsOther first, second, third, fourth, fifth, sixth, seventh, eighth, ninth, tenth, eleventh, twelfth, thirteenth;
    HoldsReference last;
};
static_assert(type_parts_v<Wide>.size > near_part_room);
static_assert(reaches_key<Wide>() && reaches_key<NamesType<Wide>>());

// A class that reaches itself ends the walk, with a key and with no key.
struct NodeWithKey {
    NodeWithKey& next;
    NamesType<NodeWithKey>& names;
    Key& key;
};
struct NodeWithNoKey {
    NodeWithNoKey& next;
    NamesType<NodeWithNoKey>& names;
    HoldsOther other;
};
template <class Derived>
struct NamesDerived {
    Derived& self;
};
struct NamesItself : NamesDerived<NamesItself> {};
static_assert(reaches_key<NodeWithKey>() && !reaches_key<NodeWithNoKey>() && !reaches_key<NamesItself>());
static_assert(reaches_key<ValueClosure<NodeWithKey>>() && !reaches_key<ValueClosure<NodeWithNoKey>>());
static_assert(!reaches_key<ReferenceClosure<NamesItself>>());

// A class that is incomplete at the first read is refused, and the kept answer
// stays a refusal after the unit completes it.
struct CompletedLater;
struct ReachesCompletedLater {
    CompletedLater& later;
};
static_assert(reaches_key<ReachesCompletedLater>() && borrows_owner<ReachesCompletedLater>());
static_assert(reaches_key<ValueClosure<ReachesCompletedLater>>());
struct CompletedLater {
    Key& key;
};
static_assert(reaches_key<ReachesCompletedLater>() && borrows_owner<ReachesCompletedLater>());
static_assert(reaches_key<ValueClosure<ReachesCompletedLater>>() && reaches_key<CompletedLater>());

// A class whose state reflection does not show is refused: it is not empty, and
// an unnamed bit-field is no member.
struct HoldsOnlyUnnamedBits {
    int : 7;
};
static_assert(reaches_key<HoldsOnlyUnnamedBits>() && borrows_owner<HoldsOnlyUnnamedBits>());
static_assert(reaches_key<ValueClosure<HoldsOnlyUnnamedBits>>() && borrows_owner<ValueClosure<HoldsOnlyUnnamedBits>>());

// A key with no mark is one that no answer names, so the gate refuses it.
struct Unmarked final {};
static_assert(reaches_loan_key_(^^int, ^^Unmarked) && reaches_loan_key_(^^HoldsOther, ^^Unmarked));
static_assert(!reaches_loan_key_(^^HoldsOther, std::meta::dealias(^^Key)));

// A root that reaches more marked classes than an answer holds is refused.
template <std::size_t Number>
struct[[= loan_key_mark{}]] Marked final {};
template <class... Types>
struct NamesTypes {};
template <std::size_t... Numbers>
NamesTypes<Marked<Numbers>...> name_marked(std::index_sequence<Numbers...>);
using FullReach = decltype(name_marked(std::make_index_sequence<key_room>{}));
using OverfullReach = decltype(name_marked(std::make_index_sequence<key_room + 1>{}));
static_assert(key_reach_v<FullReach>.size == key_room && !key_reach_v<FullReach>.is_refused);
static_assert(key_reach_v<OverfullReach>.is_refused);
static_assert(!reaches_key<FullReach>() && reaches_key<OverfullReach>());

// A keyless class answers with no walk.  A class with a key mark, a reference, a
// pointer or a borrow is no keyless class.
struct Scalars {
    int first = 0;
    long second = 0;
};
struct HoldsScalars {
    Scalars scalars;
    NamesType<PlainBrand> tag;
};
static_assert(is_keyless_v<Scalars> && is_keyless_v<HoldsScalars> && is_keyless_v<PlainBrand>);
static_assert(!is_keyless_v<Key> && !is_keyless_v<HoldsOther> && !is_keyless_v<View<int>>
              && !is_keyless_v<NamesItself>);
static_assert(!is_keyless_v<NamesType<Key>> && !is_keyless_v<NamesType<Scalars*>>);

// The borrow of a root door: the region of each part.
struct HoldsOwner {
    KeyedArray owner;
};
struct BorrowsOwner {
    KeyedArray& owner;
};
struct BorrowsHolder {
    HoldsOwner& holder;
};
template <class T>
struct[[= ::foundation::transfer::refers{}]] NamesTarget {};
template <class T>
struct[[= ::foundation::transfer::by_arguments{}]] OwnsTarget {};
template <int Number, class T>
struct[[= ::foundation::transfer::refers{}]] NamesNumberFirst {};
template <template <class> class Form, class T>
struct[[= ::foundation::transfer::refers{}]] NamesFormFirst {};
template <class T>
struct[[= ::foundation::transfer::payload_cell{}]] Cell {
    T payload;
};
static_assert(!borrows_owner<HoldsOwner>() && !borrows_owner<HoldsOwner[2]>() && !borrows_owner<KeyedArray>());
static_assert(borrows_owner<BorrowsOwner>() && borrows_owner<BorrowsHolder>() && borrows_owner<HoldsOwner*>());
static_assert(borrows_owner<void (*)(KeyedArray)>() && borrows_owner<KeyedArray HoldsOwner::*>());
static_assert(borrows_owner<NamesTarget<KeyedArray>>() && !borrows_owner<NamesType<KeyedArray>>());
static_assert(borrows_owner<NamesType<KeyedArray&>>());
static_assert(!borrows_owner<OwnsTarget<KeyedArray>>() && borrows_owner<OwnsTarget<KeyedArray>*>());
static_assert(!borrows_owner<NamesNumberFirst<1, KeyedArray>>());
static_assert(borrows_owner<NamesFormFirst<NamesType, KeyedArray>>());
static_assert(!borrows_owner<Cell<HoldsOwner>>() && !borrows_owner<Cell<HoldsOwner>*>());
static_assert(borrows_owner<Ref<HoldsOwner, OtherBrand>>() && !borrows_owner<Box<HoldsOwner>>());
static_assert(!borrows_owner<ValueClosure<HoldsOwner>>() && borrows_owner<ReferenceClosure<HoldsOwner>>());
static_assert(borrows_owner<ValueClosure<BorrowsHolder>>() && borrows_owner<ValueClosure<HoldsOwner*>>());
static_assert(!borrows_owner<ReferenceClosure<Scalars>>());

// The element of an array behind a pointer or a reference is read, and an
// incomplete element is refused.
struct OnlyDeclared;
static_assert(reaches_key<HoldsReference (*)[2]>() && !reaches_key<HoldsOther (*)[2]>());
static_assert(reaches_key<NamesType<HoldsReference (&)[2]>>() && !reaches_key<NamesType<HoldsOther (&)[2]>>());
static_assert(reaches_key<ReferenceClosure<HoldsReference[2]>>() && !reaches_key<ReferenceClosure<HoldsOther[2]>>());
static_assert(reaches_key<OnlyDeclared (*)[2]>() && borrows_owner<OnlyDeclared (*)[2]>());
static_assert(reaches_key<ReferenceClosure<OnlyDeclared>>() && borrows_owner<ReferenceClosure<OnlyDeclared>>());
static_assert(borrows_owner<TwoCaptures<int, HoldsOwner>>() && borrows_owner<TwoCaptures<BorrowsHolder, int>>());
static_assert(!borrows_owner<TwoCaptures<HoldsOwner, int>>());
static_assert(borrows_owner<HoldsOwner (*)[2]>() && !borrows_owner<Scalars (*)[2]>());

// A type argument of a plain class that is only declared is a tag, and a value
// of the class reaches no object of it.  The same argument of a class with a
// reference, of a borrow, of an owner and of a cell, and a pointer or a reference
// to the declared class, stay refused.
template <class Tag>
struct Numbered {
    int number = 0;
};
template <class Tag>
struct ReachesTag {
    Tag& target;
};
template <class Tag>
struct NamesTagBesideReference {
    int& number;
};
using DeclaredNumber = Numbered<OnlyDeclared>;
static_assert(!reaches_key<DeclaredNumber>() && !borrows_owner<DeclaredNumber>());
static_assert(!reaches_key<DeclaredNumber*>() && !borrows_owner<View<DeclaredNumber const>>());
static_assert(!reaches_key<Id<struct ElaboratedTag, unsigned>>() && !reaches_key<NamesType<DeclaredNumber>>());
static_assert(!reaches_key<ReferenceClosure<DeclaredNumber>>() && !borrows_owner<ReferenceClosure<DeclaredNumber>>());
static_assert(reaches_key<ReachesTag<OnlyDeclared>>() && borrows_owner<ReachesTag<OnlyDeclared>>());
static_assert(reaches_key<NamesTagBesideReference<OnlyDeclared>>());
static_assert(reaches_key<NamesTarget<OnlyDeclared>>() && borrows_owner<NamesTarget<OnlyDeclared>>());
static_assert(reaches_key<OwnsTarget<OnlyDeclared>>() && borrows_owner<OwnsTarget<OnlyDeclared>>());
static_assert(reaches_key<Numbered<OnlyDeclared*>>() && reaches_key<Numbered<OnlyDeclared&>>());
static_assert(borrows_owner<Numbered<OnlyDeclared*>>() && borrows_owner<Numbered<OnlyDeclared&>>());
static_assert(reaches_key<OnlyDeclared*>() && reaches_key<NamesTarget<Numbered<OnlyDeclared*>>>());

// A tag that the unit completes later gives a sound answer at each point: the
// kept answer reads nothing of it, and a later walk of another root reads its
// parts and refuses more.
struct TagCompletedLater;
static_assert(!reaches_key<Numbered<TagCompletedLater>>());
struct TagCompletedLater {
    Key& key;
};
static_assert(!reaches_key<Numbered<TagCompletedLater>>());
static_assert(reaches_key<NamesType<Numbered<TagCompletedLater>>>() && reaches_key<NamesType<TagCompletedLater>>());

// An owner of the base reads no member, so its reference to the target is no
// borrow to the walk.  KeyWalk.cpp pins its twin outside the base below.
template <class T>
struct[[= ::foundation::transfer::by_arguments{}]] RefersToTarget {
    T& target;
};
static_assert(!borrows_owner<RefersToTarget<KeyedArray>>() && borrows_owner<RefersToTarget<KeyedArray>*>());

}  // namespace detail::key_walk_checks

}  // namespace foundation::core

// A class outside the base that states by_arguments gets no trust from the
// walk, which reads its members as for each other class.  So its reference to
// the keyed owner is a borrow of the owner.
namespace key_walk_outside_checks {

namespace checks = ::foundation::core::detail::key_walk_checks;

template <class T>
struct[[= ::foundation::transfer::by_arguments{}]] RefersToTarget {
    T& target;
};
static_assert(checks::borrows_owner<RefersToTarget<checks::KeyedArray>>());

}  // namespace key_walk_outside_checks
