#pragma once

// The Ref family: one object and its owner.
//
// Ref<T, Brand> reaches one object of type T and is never null.  The brand
// is the identity of the borrow (foundation/Brand.h).  to(object) is the one
// door from an object: it gives a Ref with a fresh brand, or with the brand
// of an object that has one.  The brand is the last template parameter of
// to(), after a pack that refuses each explicit argument, so a caller cannot
// claim a brand for an object by spelling it.  to() of a temporary is
// deleted, because the Ref dangles at the end of the full expression.
//
// *ref and ref->member reach the object with no test, because a Ref has no
// empty state.  A Ref<T, Brand> converts to a Ref<T const, Brand>, and no
// conversion changes its brand.  erase_brand(ref) is the one door to the
// erased brand DefaultBrand, it refuses a scope brand, and nothing gives the
// brand back.
// same_object(a, b) tells if two Refs reach one object, and same_target(a, b)
// tells if two Options of a Ref are both empty or reach one object.  A Ref
// has no ==, no default value and no conversion from a pointer.  Option<Ref<T, Brand>> has the size of one
// pointer: the null pointer is its empty value, and no Ref holds it.  The
// word of a Ref in an atomic cell is that pointer, so an
// Atomic<Option<Ref<T, Brand>>> holds one pointer.  A Ref owns nothing, so a
// Ref that outlives its object dangles.  address_of(ref) gives the Address
// of the object, which answers is_aligned<N>() and ==, and gives no pointer
// back.
//
// mint_address_key(pointer) gives the AddressKey of an address, such as the
// address of a buffer that a foreign allocator gives.  A key answers == and
// hash() for a lookup, and it has no order.  lower_by(bytes) gives the key of
// the address some bytes lower.  AddressKey::of_null() gives the key of the
// null address as a constant, for a table that the compiler builds.  No
// pointer and no integer comes back from a key.
//
// prefetch<Access, Locality>(object) asks the cache for the line of one
// object, and prefetch_byte<Access, Locality, Offset>(object) asks for the
// line of the byte at Offset of one object.  The hint changes no value.
//
// Box<T> owns one object of type T on the heap.  The one door is
// mint_box<T>(alloc, args...): it takes the storage from the allocation
// door of foundation/core/Heap.h, builds the object from the arguments, and
// gives the Box.  The first argument is the allocation capability of
// foundation/effects, so each allocation shows at its call site.  The
// destructor destroys the object and gives the storage back to the door
// with the same size and alignment, so a Box never leaks and never frees
// twice.
//
// A Box is move-only.  A move leaves the source with no object, and the
// use-after-move guard refuses a later use of that source.  The one
// constructor of a Box from arguments takes the key of mint_box, and it
// allocates the storage itself.  So no pointer that a caller holds becomes
// a Box, and the destructor frees only storage that a Box allocated.
//
// operator* and operator-> give a borrow of the object, and view() gives
// the borrow as a Ref with the fresh brand of the call.  The overloads for
// an rvalue are deleted, because a borrow of a temporary Box dangles at the
// end of the full expression.  consume() moves the object out of a Box that
// the caller gives up, and frees the storage.  The frame door
// with_mut(std::move(box), body) lends the object to a body while the Box is
// in the frame of the door, so no move or end of the Box can come while the
// body holds the reference (the frame of a Box below).  The read door
// with_view(box, body) lends the object as a const reference in place, and
// the result of the body holds no borrow (the read frame of a Box below).
//
// C++ keeps a moved-from object alive, and no type can refuse its use.  So
// each access tests for the object in each build, and a Box with no object
// ends the process through fatal().  The test is one compare with a branch
// to a cold call.  A loop that reads the object many times takes one Ref
// with view() before the loop, and the Ref has no test.
//
// A Box moves to another thread through a ring of cells as a BoxWord (the
// word of a Box below).  release_to_word gives the object to a word of an
// authority, and claim_box gives the object of the word to a new Box.  Only
// a member of the authority builds the key of the two doors.
//
// A Hole<Size, Align> is the credit of the family: the heap storage of one
// object with no object in it.  mint_hole<Size, Align>(alloc) takes the
// storage from the allocation door, and mint_hole(std::move(box)) ends the
// object of a Box and keeps its storage.  mint_box<T>(std::move(hole),
// args...) builds a T in the storage of a Hole of the exact size and
// alignment of T, with no call of the door.  consume() gives the storage
// back.  So N steps from a Box to a Hole and back call the door one time.
//
// A Hole is linear, and consume() is its drop door.  The destructor also
// gives the storage back, so a Hole that a path drops does not leak.  A move
// leaves the source empty, and each spend of an empty Hole ends the process
// through fatal().  No constructor of a Hole takes a pointer: one allocates
// the storage, and one takes the storage of a Box.  The two constructors
// take the key that only HoleDoor builds.
//
// Box and Hole name each other.  Each declaration of the two stands in this
// header with its definition, so a unit that sees one declaration also sees
// the definition, and a second definition is a compile error.  A header that
// declared Box and did not define it would let a unit define a Box of its
// own, which the friend declaration and the mint of a Hole would accept.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Transfer.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Heap.h>
#include <foundation/core/LoanKeyTrait.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Report.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace foundation::core {

// The object of a Ref: an object type that is not an array and not
// volatile.  An abstract class is a valid target, because a Ref builds
// nothing.
template <class T>
concept RefTarget = std::is_object_v<T> && !std::is_array_v<T> && !std::is_volatile_v<T>;

template <RefTarget T, ::foundation::brand::IsBrand Brand = ::foundation::brand::DefaultBrand>
class Ref;

namespace detail {

// The source of the widening constructor of a Ref<T, Brand>.  A Ref<From,
// Brand> converts to a Ref<From const, Brand>: the conversion adds a const
// and keeps the brand.  A Ref of a target with no const widens from nothing,
// so its source is NoWidening, a class that no code builds.  No conversion
// removes a const, and no conversion changes a brand.  erase_brand is the one
// door to the erased brand.  The constructor is not a template, so the walk
// of the doors of the niche statement reads its parameter (the check of the
// statement of foundation/core/Choice.h).
//
// The two constructors are private and user-provided, and the class has no
// friend.  So std::bit_cast and a lifetime start over bytes build no
// NoWidening either.
class NoWidening final {
    constexpr NoWidening() noexcept {}
    constexpr NoWidening(NoWidening const&) noexcept {}

public:
    NoWidening& operator=(NoWidening const&) = delete("a Ref of a target with no const widens from nothing");
};

template <class T, class Brand>
using WidenedFrom = std::conditional_t<std::is_const_v<T>, Ref<std::remove_const_t<T>, Brand>, NoWidening>;

// The tag of the null value of a Ref.  Each caller can build the tag, but the
// constructor of a Ref from it is private, and only the niche door of a Ref
// calls it.  The tag stands outside Ref, so the walk of the niche statement
// reads no nested class for each brand of a Ref, which costs a list of
// members for each brand.
struct RefNullTag final {};

// The door of the constructor of a Ref from an object.  Its one member is
// private to the friends that prove each object and each brand.  The
// definition is after Ref.
class RefDoor;

// The key of the constructor of a Ref from an object.  RefDoor is its one
// friend, so each Ref from an object comes through the door.  Both
// constructors are user-provided and private, so std::bit_cast and a
// lifetime start over bytes build no key.
class [[nodiscard]] RefKey final {
    friend class RefDoor;

    constexpr RefKey() noexcept {}
    constexpr RefKey(RefKey const&) noexcept {}

public:
    RefKey& operator=(RefKey const&) = delete("a key of a Ref lives for one construction");

    // The mark of a key of the base (the check of the statement of
    // foundation/core/Choice.h).
    static constexpr niche_statement is_niche_voucher{};
};

}  // namespace detail

// The one door from an object.  The brand is the brand of a branded object,
// and the fresh brand of the call for each other object.  The pack in front
// takes each explicit template argument and refuses it, so no caller names
// the brand.
template <class... Refused, RefTarget T, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
    requires(sizeof...(Refused) == 0)
[[nodiscard]] constexpr Ref<T, ::foundation::brand::inherited_or_fresh_brand_t<T, Fresh>> to(T& object) noexcept;

template <class... Refused, class T>
    requires(!std::is_lvalue_reference_v<T>)
void to(T&& object) = delete("a Ref of a temporary dangles at the end of the full expression");

// A Ref is a reference that a value holds, so it moves between threads as a
// reference to T does (foundation/Transfer.h).
template <RefTarget T, ::foundation::brand::IsBrand Brand>
class[[= ::foundation::transfer::refers{}]] Ref {
    static_assert(!detail::is_loan_key_type_(^^T),
                  "a Ref of a LoanKey would keep a borrow of the key past the frame that lent it: borrow the key "
                  "with LoanKey<B>& for the call (THE DISPATCH BRAND of foundation/core/Frame.h)");

    T* object_ = nullptr;
    // The seal makes the assignment user-provided, so std::bit_cast cannot
    // build a Ref from the bytes of an address.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    // The null value, from the tag of the null value.  Only the two hidden
    // friends below build a Ref from it, because Ref has no other friend.  The
    // tag is detail::RefNullTag, outside the class, for the compile cost of
    // the walk of the niche statement.
    constexpr explicit Ref(detail::RefNullTag /*tag*/) noexcept {}

    // The null value of an Option of a Ref, and its test.  Each one takes the
    // key that only the storage of an Option builds, and the storage gives
    // the key only to the niche that it calls.  A niche gets no friend, so
    // the niche of a Ref reaches the null value only through these two doors.
    // The rule specialization-home refuses a niche of a Ref outside this header.
    [[nodiscard]] friend constexpr Ref niche_null_(niche_key<Ref> const& /*key*/) noexcept {
        return Ref{detail::RefNullTag{}};
    }
    [[nodiscard]] friend constexpr bool niche_is_null_(niche_key<Ref> const& /*key*/, Ref const& ref) noexcept {
        return ref.object_ == nullptr;
    }

public:
    // A Ref of an object, with the key that only RefDoor builds.  A reference
    // is never null, so no key makes a null Ref.  The niche of a Ref states
    // that no door builds the null pointer, and the payload gate checks the
    // statement against the constructors and the members of Ref (the check of
    // the statement of foundation/core/Choice.h).  So the storage of a full
    // Option does not test the pointer.  The constructor has no [[assume]]
    // that the pointer is not null.
    // That condition reads a member through this, so GCC makes it an .ASSUME
    // call.  Under -fharden-conditional-branches, that call keeps a branch
    // that the optimizer removes with no assume, such as the null test of the
    // decode of an atomic word.  No build takes that flag
    // (cmake/CodeFlags.cmake).
    constexpr Ref(T& object, detail::RefKey const& /*key*/) noexcept : object_{__builtin_addressof(object)} {}

    // Adds a const to the target, and keeps the brand.  For a target with no
    // const the constraint fails, so members_of lists no such constructor,
    // and NoWidening needs no mark of a key for the check of the statement.
    constexpr Ref(detail::WidenedFrom<T, Brand> other) noexcept
        requires std::is_const_v<T>
        : object_{__builtin_addressof(*other)} {}

    [[nodiscard]] constexpr T& operator*() const noexcept { return *object_; }
    [[nodiscard]] constexpr T* operator->() const noexcept { return object_; }
};

// The brand of a Ref that ref_of_static gives.  The template
// argument of that door names an object of static storage, which lives for
// the whole process, so such a Ref never dangles.  to() gives the brand that
// the type of an object states, so a type that states this brand as its
// brand_type gets it for a local object too.
struct StaticStorageBrand final {};

// The type of a Box: an object type that is not an array and not
// cv-qualified.  The type can be one that is only declared, so a Box can be
// a member of a class in a header that does not define the type of its
// object.
template <class T>
concept BoxPayload = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>;

// The owner of one object on the heap (the definition below).  RefDoor names
// it as a friend for view(), so this declaration stands before the door.
template <BoxPayload T>
class Box;

// The payload of an Option as a Ref (the definition below).  RefDoor names it
// as a friend, so this declaration stands before the door, and the default
// argument of the brand belongs to it.
template <class... Refused, ChoicePayload T, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
    requires(sizeof...(Refused) == 0) && (detail::option_form_of<T> != detail::OptionForm::owning)
[[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
as_ref(Option<T> const& option) noexcept;

namespace detail {

// True when brand is the brand of an arena: a member tag, a family tag or an
// epoch brand of foundation/core/BumpArena.h, each of which carries its mark of
// foundation/Brand.h.  A Ref of an arena keeps the brand of its arena, so a
// store of the Ref names its arena, and no erased Ref of an arena exists.
[[nodiscard]] consteval bool names_arena_brand_(std::meta::info brand) {
    namespace brand_marks = ::foundation::brand;
    return ::foundation::linear::carries_annotation(brand, ^^brand_marks::arena_member_brand)
        || ::foundation::linear::carries_annotation(brand, ^^brand_marks::arena_family_brand)
        || ::foundation::linear::carries_annotation(brand, ^^brand_marks::epoch_brand);
}

}  // namespace detail

// A brand that erase_brand drops: a brand of one instance that is not a scope
// brand, names none, and is no brand of an arena.  The rule brand-escape
// refuses each value of a scope brand that outlives its scope, and an erased
// Ref names no brand that the rule could read.  So a scope brand has no
// erasure.  A Ref of an arena keeps the brand of its arena, so a field that
// stores it names the arena that holds its object.
template <class Brand>
concept ErasableBrand = ::foundation::brand::IsFreshBrand<Brand> && !detail::names_scope_brand(^^Brand)
                     && !detail::names_arena_brand_(^^Brand);

// The door that drops the brand of a Ref (the definition below).  RefDoor
// names it as a friend, so this declaration stands before the door.
template <RefTarget T, ErasableBrand Brand>
[[nodiscard]] constexpr auto erase_brand(Ref<T, Brand> ref) noexcept;

// The Ref of an object of static storage (the definition below).  RefDoor
// names it as a friend, so this declaration stands before the door.
template <auto& Object>
    requires RefTarget<std::remove_reference_t<decltype(Object)>>
[[nodiscard]] constexpr Ref<std::remove_reference_t<decltype(Object)>, StaticStorageBrand> ref_of_static() noexcept;

// Doors of the owners above this header
// An owner in a header above this one makes a Ref of an object that it holds,
// and an invariant of the owner proves the object and the brand, such as an
// element below the count of a run or a slot that holds the value of a key.
// Such an owner reaches the door of a Ref through one plain door class.  This
// header declares the class, and RefDoor names it as a friend.  The header of
// the owner defines it, and the owner is its friend.  The comment of each door
// states the invariant of its owner.  A home owner row of utils/rule-table.txt
// keeps the one definition of each door in that header, so no other file
// defines a second door of the same name.  The door of Region.h also holds the
// body of each get of a run, and the doors of Array.h and List.h reach it.
class RegionRefDoor;
class ListRefDoor;
class BrandedRefDoor;
class WalkRefDoor;
class OneOfRefDoor;
class WindowLoanRefDoor;
class SlotMapRefDoor;
class ImmortalRefDoor;
class AllocWindowRefDoor;
class ArenaRefDoor;
class HeapScopeRefDoor;
class ScopedViewRefDoor;
class ChaseLevDequeRefDoor;
class SpscSplitRefDoor;
class SchedProofRefDoor;
class LazyRefDoor;
class WriteOnceRefDoor;

namespace detail {

// The Ref of the word of an atomic cell (the definition below).  RefDoor
// names it as a friend, so this declaration stands before the door.
template <RefTarget T, ::foundation::brand::IsBrand Brand>
[[nodiscard]] constexpr Ref<T, Brand> ref_of_word_(atomic_word_key<Ref<T, Brand>> const& key, T* word) noexcept;

// The door of the constructor of a Ref from an object.  Its one member is
// private, and each friend names the proof of the object and of the brand:
//   - to() gives the brand of a branded object or the fresh brand of its call.
//   - as_ref, erase_brand and view() of a Box give the payload of an Option,
//     the object of a Ref and the object of a Box, with the fresh brand of
//     their call or with the erased brand.
//   - ref_of_word_ takes the key of the word of a cell, and only a cell builds
//     the key, from a word that to_word gave for a Ref of the brand.
//   - ref_of_static takes the object as its template argument, which proves
//     static storage, and gives the one brand of static storage.
//   - The door class of each owner above this header lends the objects of its
//     owner (the doors of the owners above this header).
// So no other code of the base makes a Ref of a brand that it names.  An
// explicit specialization of lend is a member of the door, so it would build
// the key with each brand.  The rule specialization-home refuses such a
// specialization outside this header.  The key holds only the constructor
// from a reference, so a specialization cannot make a null Ref either.
class RefDoor final : ::foundation::NoObject<RefDoor> {
    template <class... Refused, RefTarget T, ::foundation::brand::IsBrand Fresh>
        requires(sizeof...(Refused) == 0)
    friend constexpr auto ::foundation::core::to(T& object) noexcept
        -> Ref<T, ::foundation::brand::inherited_or_fresh_brand_t<T, Fresh>>;
    template <class... Refused, ChoicePayload T, ::foundation::brand::IsBrand Fresh>
        requires(sizeof...(Refused) == 0) && (detail::option_form_of<T> != detail::OptionForm::owning)
    friend constexpr auto ::foundation::core::as_ref(Option<T> const& option) noexcept
        -> Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>;
    template <RefTarget T, ErasableBrand Brand>
    friend constexpr auto ::foundation::core::erase_brand(Ref<T, Brand> ref) noexcept;
    template <BoxPayload T>
    friend class ::foundation::core::Box;
    template <RefTarget T, ::foundation::brand::IsBrand Brand>
    friend constexpr auto ref_of_word_(atomic_word_key<Ref<T, Brand>> const& key, T* word) noexcept -> Ref<T, Brand>;
    template <auto& Object>
        requires RefTarget<std::remove_reference_t<decltype(Object)>>
    friend constexpr auto ::foundation::core::ref_of_static() noexcept
        -> Ref<std::remove_reference_t<decltype(Object)>, StaticStorageBrand>;

    friend class ::foundation::core::RegionRefDoor;
    friend class ::foundation::core::ListRefDoor;
    friend class ::foundation::core::BrandedRefDoor;
    friend class ::foundation::core::WalkRefDoor;
    friend class ::foundation::core::OneOfRefDoor;
    friend class ::foundation::core::WindowLoanRefDoor;
    friend class ::foundation::core::SlotMapRefDoor;
    friend class ::foundation::core::ImmortalRefDoor;
    friend class ::foundation::core::AllocWindowRefDoor;
    friend class ::foundation::core::ArenaRefDoor;
    friend class ::foundation::core::HeapScopeRefDoor;
    friend class ::foundation::core::ScopedViewRefDoor;
    friend class ::foundation::core::ChaseLevDequeRefDoor;
    friend class ::foundation::core::SpscSplitRefDoor;
    friend class ::foundation::core::SchedProofRefDoor;
    friend class ::foundation::core::LazyRefDoor;
    friend class ::foundation::core::WriteOnceRefDoor;

    template <::foundation::brand::IsBrand Brand, RefTarget T>
    [[nodiscard]] static constexpr Ref<T, Brand> lend(T& object) noexcept {
        return Ref<T, Brand>{object, RefKey{}};
    }
};

}  // namespace detail

// A reference names an object, and the build keeps each test of an address
// for null (-fno-delete-null-pointer-checks), so an assumption states the
// fact, as erase_brand and as_ref do, and an Option of the result takes no
// test of the null pointer.
template <class... Refused, RefTarget T, ::foundation::brand::IsBrand Fresh>
    requires(sizeof...(Refused) == 0)
[[nodiscard]] constexpr Ref<T, ::foundation::brand::inherited_or_fresh_brand_t<T, Fresh>> to(T& object) noexcept {
    [[assume(__builtin_addressof(object) != nullptr)]];
    return detail::RefDoor::lend<::foundation::brand::inherited_or_fresh_brand_t<T, Fresh>>(object);
}

// The payload of an Option as an Option of a Ref of a const T with the
// fresh brand of the call, or no value for an empty Option: one test of the
// state, as get() of a OneOf gives an alternative.  The Option keeps its
// payload, and the Ref reads it.  The loop of the Option gives the payload,
// so the Ref and its brand leave only the body of this function, whose own
// template argument the brand is.  The pack refuses each explicit argument,
// so no caller names the brand.  The function is free and not a member,
// because this header includes Choice.h, so the class Option cannot name a
// Ref.  The addresses of the Option and of its payload are not null, because
// a reference names an object.  The build keeps each test of an address for
// null (-fno-delete-null-pointer-checks), so two assumptions state those
// facts, and the full Option of the Ref takes no test of the null pointer.
//
// Only a niche or a plain Option gives its payload as a Ref, as only they
// have the loop.  An owning Option counts each lend of its payload (the lend
// count of Choice.h), and a Ref outlives each count: a write of the Option
// would then end the object that the Ref names, with no check.
template <class... Refused, ChoicePayload T, ::foundation::brand::IsBrand Fresh>
    requires(sizeof...(Refused) == 0) && (detail::option_form_of<T> != detail::OptionForm::owning)
[[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
as_ref(Option<T> const& option) noexcept {
    [[assume(__builtin_addressof(option) != nullptr)]];
    for (T const& payload : option) {
        [[assume(__builtin_addressof(payload) != nullptr)]];
        return Option<Ref<T const, Fresh>>::some(detail::RefDoor::lend<Fresh>(payload));
    }
    return none;
}

template <class... Refused, ChoicePayload T>
    requires(detail::option_form_of<T> == detail::OptionForm::owning)
void as_ref(Option<T> const& option) =
    delete("a Ref of the payload of an owning Option outlives each lend that the Option counts, and a write of the "
           "Option then ends the object that the Ref names: read the payload with match");

template <class... Refused, class T>
void as_ref(Option<T> const&& option) =
    delete("a Ref into a temporary Option dangles at the end of the full expression: name the Option");

// True when the two Refs reach one object.  The two targets are one type,
// with or without a const, and the two brands can differ.
template <class First, class FirstBrand, class Second, class SecondBrand>
    requires std::same_as<std::remove_const_t<First>, std::remove_const_t<Second>>
[[nodiscard]] constexpr bool same_object(Ref<First, FirstBrand> first, Ref<Second, SecondBrand> second) noexcept {
    return __builtin_addressof(*first) == __builtin_addressof(*second);
}

// True when the two Options hold no Ref, or hold two Refs of one object.  The
// two targets are one type, with or without a const, and the two brands can
// differ.  An empty slot of a table or a mark of a mailbox compares in this
// way.  The cost is the compare of two pointers, because the empty Option is
// the null pointer.
template <class First, class FirstBrand, class Second, class SecondBrand>
    requires std::same_as<std::remove_const_t<First>, std::remove_const_t<Second>>
[[nodiscard]] constexpr bool same_target(Option<Ref<First, FirstBrand>> const& first,
                                         Option<Ref<Second, SecondBrand>> const& second) noexcept {
    for (Ref<First, FirstBrand> const& first_ref : first) {
        for (Ref<Second, SecondBrand> const& second_ref : second) {
            return same_object(first_ref, second_ref);
        }
        return false;
    }
    return second.is_none();
}

// The door that drops the brand of a Ref.  It gives a Ref of the same object
// on the erased identity DefaultBrand, which each erased Ref of the target
// type shares, and nothing gives the brand back.  Call it only where the
// holder cannot name the brand, such as a slot of a table that holds Refs of
// many sites.  A search for this name lists each site that drops one.  The
// result is a Ref<T, DefaultBrand>, and the body names its brand, so the
// erased brand has one spelling in this door.  No Ref holds the null pointer,
// and the build keeps each test of an address for null
// (-fno-delete-null-pointer-checks), so an assumption states the fact, and an
// Option of the result takes no test of the null pointer.
template <RefTarget T, ErasableBrand Brand>
[[nodiscard]] constexpr auto erase_brand(Ref<T, Brand> ref) noexcept {
    T& object = *ref;
    [[assume(__builtin_addressof(object) != nullptr)]];
    return detail::RefDoor::lend<::foundation::brand::DefaultBrand>(object);
}

// The Ref of Object with the brand of static storage.  A template argument
// of a reference type names an object of static storage, so the object lives
// for the whole process, and the Ref never dangles.  A local object, a
// temporary and a thread_local object are no template argument, so their
// call does not compile.  The constraint refuses a function, which a
// reference argument also names.  An object of static or thread storage
// can hold such a Ref (the statics of the rule view-escape).
template <auto& Object>
    requires RefTarget<std::remove_reference_t<decltype(Object)>>
[[nodiscard]] constexpr Ref<std::remove_reference_t<decltype(Object)>, StaticStorageBrand> ref_of_static() noexcept {
    return detail::RefDoor::lend<StaticStorageBrand>(Object);
}

namespace detail {

// The door of address_of, and the key of the constructor of an Address.
// AddressDoor is the one friend of the key, and only address_of calls the
// door.  The door and the key hold no template, so no explicit
// specialization of a template reaches the key.  The door is in namespace
// detail, and a use of that namespace outside the base is a finding.  Such
// a use can still build an Address from each integer.
struct AddressDoor;

class [[nodiscard]] AddressOfKey final {
    friend struct AddressDoor;

    constexpr AddressOfKey() noexcept {}
    constexpr AddressOfKey(AddressOfKey const&) noexcept {}

public:
    AddressOfKey& operator=(AddressOfKey const&) = delete("a key of an Address lives for one construction");
};

struct AddressDoor {
    [[nodiscard]] static constexpr AddressOfKey key() noexcept { return AddressOfKey{}; }
};

// The door of mint_address_key and of the atomic word of a key, and the one
// friend of AddressKey.  Only the two of them call it.  The door holds no
// template, so no explicit specialization of a template reaches the
// constructor of a key.  The door is in namespace detail, and a use of that
// namespace outside the base is a finding.
struct AddressKeyDoor;

// The word of an AddressKey in an atomic cell: the bits of its address.  The
// enum is in namespace detail, and a use of that namespace outside the base is
// a finding, so no integer comes back from a key.
enum class AddressKeyWord : std::uintptr_t {
};

}  // namespace detail

// The address of one object, which address_of(ref) gives.  It answers an
// alignment query and compares with a different address.  An address
// changes from run to run, so it has no hash and no order, and no pointer
// comes back from it.  The seal makes the assignment user-provided, so
// std::bit_cast cannot build an Address from an integer.
class Address {
    std::uintptr_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    // The address of one object, with the key that only address_of takes.
    constexpr Address(std::uintptr_t value, detail::AddressOfKey const& /*key*/) noexcept : value_{value} {}

    // True when the address is a multiple of Alignment, a power of two.
    template <std::size_t Alignment>
        requires(Alignment != 0 && (Alignment & (Alignment - 1)) == 0)
    [[nodiscard]] constexpr bool is_aligned() const noexcept {
        return (value_ & (Alignment - 1)) == 0;
    }

    [[nodiscard]] friend constexpr bool operator==(Address const& left, Address const& right) noexcept {
        return left.value_ == right.value_;
    }
};

// The address of the object of a Ref.  The address of an object is no
// constant of the compile, so the function runs only at run time.
template <RefTarget T, ::foundation::brand::IsBrand Brand>
[[nodiscard]] Address address_of(Ref<T, Brand> ref) noexcept {
    return Address{__builtin_bit_cast(std::uintptr_t, __builtin_addressof(*ref)), detail::AddressDoor::key()};
}

// The type of the object at an address that a key names: an object type or
// void.  So a key comes from a pointer to an object or from a void pointer,
// and not from a pointer to a function.
template <class T>
concept AddressKeyTarget = std::is_object_v<T> || std::is_void_v<T>;

// An address as the key of a lookup, such as the address of a buffer that a
// foreign allocator gives.  A key compares for equality and gives a hash,
// and it has no order.  An address changes from run to run, and its hash
// changes with it.  So a table that the hash places gives no order to read,
// and a key that outlives the process is a content hash, not an AddressKey.
// No pointer and no integer comes back from a key.  mint_address_key is the
// one door from a pointer, and lower_by gives a key from a key.  The seal
// makes the assignment user-provided, so std::bit_cast cannot build a key
// from an integer or read its integer.
class AddressKey {
    std::uintptr_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend struct detail::AddressKeyDoor;

    constexpr explicit AddressKey(std::uintptr_t value) noexcept : value_{value} {}

public:
    // The key of the address zero as a constant: the key that
    // mint_address_key gives for a null pointer.  A table that the compiler
    // builds, such as a constinit cache, holds it in each empty slot.  A key
    // has no default constructor, so each key of a slot names its address on
    // purpose, and the null address has this one name.
    [[nodiscard]] static consteval AddressKey of_null() noexcept { return AddressKey{0}; }

    // The hash of the address.  The multiply by the odd constant
    // 0x9E3779B97F4A7C15, 2^64 divided by the golden ratio, mixes the bits of
    // the address into the high half of the product.  The fold then adds the
    // high half into the low half with an exclusive or.  So the low bits of
    // the hash depend on the high bits of the address too, and a table of a
    // power-of-two size takes them with a mask.  The cost is one multiply,
    // one shift and one exclusive or.
    [[nodiscard]] constexpr std::uint64_t hash() const noexcept {
        std::uint64_t const product = static_cast<std::uint64_t>(value_) * 0x9E3779B97F4A7C15ULL;
        return product ^ (product >> 32);
    }

    // The key of the address bytes below this address, such as the first
    // byte of the storage of a tensor from the address of its first element.
    // A key reads no object, so the address below can name no object of the
    // program.  The subtraction wraps, so each count gives a key.  No pointer
    // and no integer comes back.
    [[nodiscard]] constexpr AddressKey lower_by(std::uint64_t bytes) const noexcept {
        return AddressKey{value_ - static_cast<std::uintptr_t>(bytes)};
    }

    [[nodiscard]] friend constexpr bool operator==(AddressKey const& left, AddressKey const& right) noexcept {
        return left.value_ == right.value_;
    }
};

namespace detail {

struct AddressKeyDoor {
    // The key of the address that pointer holds.  The parameter takes each
    // pointer to an object, a const one or a volatile one.
    [[nodiscard]] static AddressKey key_of(void const volatile* pointer) noexcept {
        return AddressKey{__builtin_bit_cast(std::uintptr_t, pointer)};
    }

    // The word of a key, and the key of a word that a cell holds.  Only a cell
    // makes the key of the word, and a cell holds only words that word_of gave.
    [[nodiscard]] static constexpr AddressKeyWord word_of(AddressKey const& key) noexcept {
        return AddressKeyWord{key.value_};
    }
    [[nodiscard]] static constexpr AddressKey key_of_word(atomic_word_key<AddressKey> const& /*cell*/,
                                                          AddressKeyWord word) noexcept {
        return AddressKey{std::to_underlying(word)};
    }
};

}  // namespace detail

// The word of an AddressKey in an atomic cell (the word of a value of
// Atomic.h).  So a table that threads share holds its keys in cells, and a
// compare of a key that a cell gives is a compare of one word.  Each address is
// a key, and the key of the null address gives the empty word.  So a cell of an
// Option of an AddressKey ends the process on a store of that key.
template <>
struct atomic_word<AddressKey> {
    using word_type = detail::AddressKeyWord;
    static constexpr word_type empty_word{};

    [[nodiscard]] static constexpr word_type to_word(AddressKey const& key) noexcept {
        return detail::AddressKeyDoor::word_of(key);
    }

    [[nodiscard]] static constexpr AddressKey from_word(atomic_word_key<AddressKey> const& cell,
                                                        word_type word) noexcept {
        return detail::AddressKeyDoor::key_of_word(cell, word);
    }
};

// The key of the address that pointer holds.  A null pointer gives the key
// of the address zero, which is equal only to the key of a null pointer.
// The key reads no object, so a pointer whose object has ended still gives
// its key.  The address of an object is no constant of the compile, so the
// mint runs only at run time.
template <AddressKeyTarget T>
[[nodiscard]] AddressKey mint_address_key(T const* pointer) noexcept {
    return detail::AddressKeyDoor::key_of(pointer);
}

// The access that a prefetch prepares.  A write asks for the line in the
// state that lets a store change it with no second request.
enum class PrefetchAccess : unsigned char {
    read = 0,
    write = 1,
};

// Asks the cache for the line that holds the first byte of one object.
// Locality is the third operand of __builtin_prefetch, from 0 to 3: 0 keeps
// the line for one use, and 3 keeps it in each level of the cache.  The hint
// changes no value, so the program gives the same result with or without
// it.  The function always inlines, so a call gives the one instruction of
// the builtin.
template <PrefetchAccess Access, int Locality, RefTarget T>
    requires(Access == PrefetchAccess::read || Access == PrefetchAccess::write) && (Locality >= 0 && Locality <= 3)
[[gnu::always_inline]] inline void prefetch(T const& object) noexcept {
    __builtin_prefetch(__builtin_addressof(object), static_cast<int>(Access), Locality);
}

// A temporary ends at the end of the full expression, so its line has no
// later use.
template <PrefetchAccess Access, int Locality, class T>
    requires(!std::is_lvalue_reference_v<T>)
void prefetch(T&& object) = delete("a prefetch of a temporary asks for a line that the full expression ends: "
                                   "prefetch an object that has a name");

// Asks the cache for the line that holds the byte at Offset of one object,
// such as the second line of an object that is wider than one line.  The
// offset is below the size of the object, so the byte is a byte of the
// object.  The hint changes no value, and a call gives the one instruction
// of the builtin.
template <PrefetchAccess Access, int Locality, std::size_t Offset, RefTarget T>
    requires(Access == PrefetchAccess::read || Access == PrefetchAccess::write) && (Locality >= 0 && Locality <= 3)
         && (Offset < sizeof(T))
[[gnu::always_inline]] inline void prefetch_byte(T const& object) noexcept {
    __builtin_prefetch(static_cast<char const*>(static_cast<void const*>(__builtin_addressof(object))) + Offset,
                       static_cast<int>(Access), Locality);
}

template <PrefetchAccess Access, int Locality, std::size_t Offset, class T>
    requires(!std::is_lvalue_reference_v<T>)
void prefetch_byte(T&& object) = delete("a prefetch of a temporary asks for a line that the full expression ends: "
                                        "prefetch an object that has a name");

// The empty value of an Option of a Ref: the null pointer, which no Ref
// holds.  Option<Ref<T, Brand>> then has the size of one pointer.  The
// niche gives the key of its call to the hidden friends of Ref.
//
// No door of Ref builds the null pointer.  RefDoor builds a Ref from a
// reference, the widening constructor copies the address of a Ref, the seal
// refuses std::bit_cast and the checked lifetime start, and only the storage
// of an Option builds the key of niche_null_.  The payload gate checks the
// statement against the constructors and the members of Ref, and its place in
// this header, at compile time.  So a
// loop over a mutable Option of a Ref gets a cursor that writes the Ref, and
// the storage of a full Option tests no Ref for the null pointer.
template <RefTarget T, ::foundation::brand::IsBrand Brand>
struct niche<Ref<T, Brand>> {
    static constexpr niche_statement is_empty_never_built{};
    [[nodiscard]] static constexpr Ref<T, Brand> empty(niche_key<Ref<T, Brand>> const& key) noexcept {
        return niche_null_(key);
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<Ref<T, Brand>> const& key,
                                                 Ref<T, Brand> const& ref) noexcept {
        return niche_is_null_(key, ref);
    }
};

namespace detail {

// The Ref of a word that a cell gives back to from_word below.  The key
// proves the word: only a cell builds the key, and the cell holds only words
// that to_word gave for a Ref of the brand.  So the word names a live object
// and is not null.  RefDoor names this function as a friend.  The door names
// no specialization of atomic_word, because each header can specialize the
// template.
template <RefTarget T, ::foundation::brand::IsBrand Brand>
[[nodiscard]] constexpr Ref<T, Brand> ref_of_word_(atomic_word_key<Ref<T, Brand>> const& /*key*/, T* word) noexcept {
    return RefDoor::lend<Brand>(*word);
}

}  // namespace detail

// The word of a Ref in an atomic cell (the word of a value of Atomic.h): the
// pointer of its object.  No Ref holds the null pointer, so the null pointer
// is the empty word, and the cell of an Option of a Ref holds one pointer.
// Only a cell makes the key of from_word, and a cell holds only words that
// to_word gave.  So from_word lends the object of the word again, with the
// brand of the Ref, through detail::ref_of_word_, which takes the key.  The
// word of a Ref is the address of its object, and only the null Ref of the
// niche holds the null pointer, so the protocol states the word of a stated
// payload of Atomic.h.
template <RefTarget T, ::foundation::brand::IsBrand Brand>
struct atomic_word<Ref<T, Brand>> {
    using word_type = T*;
    static constexpr word_type empty_word = nullptr;
    static constexpr niche_statement is_empty_word_of_niche{};

    [[nodiscard]] static constexpr word_type to_word(Ref<T, Brand> const& ref) noexcept {
        return __builtin_addressof(*ref);
    }

    [[nodiscard]] static constexpr Ref<T, Brand> from_word(atomic_word_key<Ref<T, Brand>> const& key,
                                                           word_type word) noexcept {
        return detail::ref_of_word_<T, Brand>(key, word);
    }
};

// The object of a Box where a Box makes it or ends it: a payload of a
// complete type that is not abstract, with a destructor that does not throw.
// The mints and the end of the object test it.  The test of the size comes
// first, so the two traits never read a type that is only declared.  So an
// owner of a Box of a declared type defines its destructor where the type is
// complete.
template <class T>
concept CompleteBoxPayload =
    BoxPayload<T> && requires { sizeof(T); } && !std::is_abstract_v<T> && std::is_nothrow_destructible_v<T>;

// The size and the alignment of a Hole: a size of at least one byte, and an
// alignment that is a power of two.
template <std::size_t Size, std::size_t Align>
concept HoleShape = Size > 0 && Align > 0 && (Align & (Align - 1)) == 0;

namespace detail {

// The layout of the storage of a Hole, a constant.  HoleShape holds the two
// facts that the door of the layout tests.
template <std::size_t Size, std::size_t Align>
    requires HoleShape<Size, Align>
inline constexpr ::foundation::alloc::Layout hole_layout_ =
    ::foundation::alloc::mint_layout(ByteCount{Size}, ::foundation::alloc::Alignment{Align})
        .expect("a HoleShape is a layout");

}  // namespace detail

// Box takes a Hole in a constructor, and this header defines Hole after Box.
template <std::size_t Size, std::size_t Align>
    requires HoleShape<Size, Align>
class Hole;

template <class T, class Marker, class... Args>
concept CanMintBox = CompleteBoxPayload<T> && AllocationCapability<Marker> && std::constructible_from<T, Args&&...>;

// True when a Box of T is linear: T is complete where the Box is first
// named, and T is linear.  A Box of a linear payload holds a value that a
// path must consume, so the Box is linear too, as each owner of an element
// is.  A Box that a unit first names where T is only declared is not
// linear, so the end of such a Box stops the build when T is linear.
template <class T>
[[nodiscard]] consteval bool is_linear_box_payload() {
    return std::meta::is_complete_type(^^T) && ::foundation::linear::is_linear_type(^^T);
}

// The one fact of linearity of a Box of T: the answer of
// is_linear_box_payload where a unit first names the Box.  The annotation of
// the Box and its two assignments read this variable, so the three agree, and
// no constraint of a member of the Box walks the payload again.
template <class T>
inline constexpr bool is_linear_box = is_linear_box_payload<T>();

// The object of a Box that a path other than a consume can end: a complete
// payload that is not linear, because the end of a linear object drops it.
// The mint of a Hole ends the object.  A word of a Box is not linear, and the
// authority of a word claims each word that it still holds at its own end,
// so the Box of that claim ends the object (the word of a Box).
template <class T>
concept DroppableBoxPayload = CompleteBoxPayload<T> && !is_linear_box_payload<T>();

namespace detail {

// The door of the two mints of a Hole, and the key of the constructors of a
// Hole.  HoleDoor is the one friend of the key.  The door and the key hold
// no template, so no explicit specialization of a template reaches the key.
// A use of namespace detail outside the base gets only a Hole of new
// storage, or a Hole of the storage of a Box that it gives up.
struct HoleDoor;

class [[nodiscard]] HoleKey final {
    friend struct HoleDoor;

    constexpr HoleKey() noexcept {}
    constexpr HoleKey(HoleKey const&) noexcept {}

public:
    HoleKey& operator=(HoleKey const&) = delete("a key of a Hole lives for one construction");
};

struct HoleDoor {
    [[nodiscard]] static constexpr HoleKey key() noexcept { return HoleKey{}; }
};

// The door of mint_box, and the key of the constructor of a Box.  BoxDoor is
// the one friend of the key, and only mint_box calls the door.  The door and
// the key hold no template, so no explicit specialization of a template
// reaches the key.  The constructor that takes the key allocates the storage
// and builds the object itself.  So a use of namespace detail outside the
// base gets only a Box of new storage, with no allocation capability at its
// call site.
struct BoxDoor;

class [[nodiscard]] BoxKey final {
    friend struct BoxDoor;

    constexpr BoxKey() noexcept {}
    constexpr BoxKey(BoxKey const&) noexcept {}

public:
    BoxKey& operator=(BoxKey const&) = delete("a key of a Box lives for one construction");
};

struct BoxDoor {
    [[nodiscard]] static constexpr BoxKey key() noexcept { return BoxKey{}; }
};

// The form of the constructor of a Box that builds its object from a prvalue
// that a callable gives (the mint_box of an arena of
// foundation/core/BumpArena.h).
struct BoxBuild final {};

namespace owner {

// The field of a Box: its object, or null when it holds none.
template <BoxPayload T>
struct BoxState {
    using owner_state_mark = OwnerStateMark;

    T* object = nullptr;

    [[nodiscard]] static constexpr BoxState emptied(BoxState const& /*state*/) noexcept { return BoxState{}; }

    // Ends the object and gives its storage back.  A unit where T is only
    // declared gets one error here, and no error of the size or the
    // destructor of T.
    static void release(BoxState const& state) noexcept {
        if constexpr (CompleteBoxPayload<T>) {
            static_assert(!::foundation::linear::is_linear_type(^^T)
                              || ::foundation::linear::is_marked_linear(^^Box<T>),
                          "a Box of a linear payload is linear only when a unit first names the Box where the payload "
                          "is complete: name the Box first after the definition of the payload");
            if (state.object != nullptr) {
                state.object->~T();
                heap_release_(static_cast<void*>(state.object), ::foundation::alloc::Layout::of<T>());
            }
        } else {
            static_assert(false, "a Box ends its object only where the type of the object is complete: define the "
                                 "destructor of the owner where the type is defined");
        }
    }
};

// The field of a Hole: its storage, or null when it holds none.
template <std::size_t Size, std::size_t Align>
struct HoleState {
    using owner_state_mark = OwnerStateMark;

    void* storage = nullptr;

    [[nodiscard]] static constexpr HoleState emptied(HoleState const& /*state*/) noexcept { return HoleState{}; }

    static void release(HoleState const& state) noexcept {
        if (state.storage != nullptr) {
            heap_release_(state.storage, hole_layout_<Size, Align>);
        }
    }
};

}  // namespace owner

}  // namespace detail

template <BoxPayload T>
class Box;

// Word of a Box
//
// A channel moves a Box from one thread to another through a ring of cells,
// and a cell holds only a trivially copyable value, which a Box is not.  So
// the producer gives the object of its Box to a BoxWord, and the Box then
// holds no object.  The ring carries the word, and the consumer gives the
// word to claim_box, which gives a new Box of the same object.
//
// A word copies as its address, and its type cannot stop a second copy.  So
// the word names an authority in its type, and each door takes the key of
// that authority.  box_word_key<Authority> has one friend, Authority, so only
// a member of the authority builds a key, and only the authority claims the
// words that it made.  The authority gives each word to claim_box at most one
// time.  At its own end it claims each word that it still holds, so the Box
// of the claim ends the object.  A word that no claim takes leaks its object.
// No path consumes the object of such a word, so only a Box of a payload that
// is not linear gives a word (DroppableBoxPayload).
//
// The empty word holds no object.  A cell of a ring holds it before its
// first write, and a claim of it ends the process through fatal().
template <class Authority>
    requires std::is_class_v<Authority>
class [[nodiscard]] box_word_key {
    friend Authority;

    // Both constructors are user-provided, so no byte copy and no lifetime
    // start over a buffer builds a key.
    constexpr box_word_key() noexcept {}
    constexpr box_word_key(box_word_key const&) noexcept {}

public:
    box_word_key& operator=(box_word_key const&) = delete("a key of a box word lives for one call of a door");
};

// The object of a Box, given to the words of one authority (the word of a
// Box).  Only a Box builds a word that holds an object, and only a Box reads
// the object of a word.  The type is trivially copyable, so a ring cell
// holds it.  A word owns the object behind its pointer, as the Box that made
// it did, so it moves between threads as its payload does.  The authority
// names no object that the word holds, and it can be a class that its own
// body names while it is incomplete (foundation/Transfer.h, rule 6).
template <BoxPayload T, class Authority>
    requires std::is_class_v<Authority>
class[[= ::foundation::transfer::by_arguments{}]] BoxWord {
    template <BoxPayload U>
    friend class Box;

    T* object_ = nullptr;

    constexpr explicit BoxWord(T* object) noexcept : object_{object} {}

public:
    // The empty word.
    constexpr BoxWord() noexcept = default;

    // True for the empty word.
    [[nodiscard]] constexpr bool is_empty() const noexcept { return object_ == nullptr; }
};

// The core of foundation/core/Owner.h gives the moves and the end of a Box.
// A Box of a linear payload is linear, and it has no move assignment,
// because the assignment ends the payload that the target holds.  A Box owns
// its object alone, so it moves between threads as its payload does
// (foundation/Transfer.h).
template <BoxPayload T>
class [[nodiscard]][[= ::foundation::linear::linear_when<is_linear_box<T>>]][[= ::foundation::transfer::by_arguments{}]]
    Box final : public OwnerCore<detail::owner::BoxState<T>, Box<T>> {
    static_assert(!detail::is_loan_key_type_(^^T),
                  "a Box of a LoanKey would carry the key past the frame that lent it (THE DISPATCH BRAND of "
                  "foundation/core/Frame.h)");

    using State = detail::owner::BoxState<T>;
    using Core = OwnerCore<State, Box>;

    // A moved-from or consumed Box holds no object.  The use-after-move
    // guard refuses a use of it that it can see, and each build tests it
    // here.
    [[nodiscard]] T* held_() const noexcept {
        if (this->state_.object == nullptr) [[unlikely]] {
            fatal("a borrow of a Box that holds no object: the Box was moved from or consumed");
        }
        return this->state_.object;
    }

    // The mint of a Hole from a Box calls vacate_ through the constructor of
    // the Hole.  Hole is a class template, so no unit outside its home can
    // specialize it.
    template <std::size_t Size, std::size_t Align>
        requires HoleShape<Size, Align>
    friend class Hole;

    // Ends the object and gives the storage to a Hole.  The Box then holds
    // no object.
    [[nodiscard]] void* vacate_() noexcept {
        T* const object = held_();
        object->~T();
        this->state_.object = nullptr;
        return static_cast<void*>(object);
    }

    // The object of a word that a claim takes.  The empty word holds none.
    [[nodiscard]] static T* claimed_(T* object) noexcept {
        if (object == nullptr) [[unlikely]] {
            fatal("a claim of the empty box word: no Box gave an object to the word");
        }
        return object;
    }

    // The object of a Box in the frame of its door (the frame of a Box), with
    // the key that only a frame door builds.  A Box with no object ends the
    // process here, because a moved-from Box can still come to the door.
    [[nodiscard]] friend T& frame_object_(detail::FrameKey const& /*key*/, Box& box) noexcept { return *box.held_(); }

public:
    // Takes the storage of the size and the alignment of T from the
    // allocation door, and builds the object from the arguments in it.  Only
    // mint_box holds the key.  An explicit specialization of this constructor
    // is a specialization of Box outside its home, and the rule
    // specialization-home refuses it in each build.
    template <class... Args>
        requires CompleteBoxPayload<T> && std::constructible_from<T, Args&&...>
    explicit Box(detail::BoxKey const& /*key*/, Args&&... args) noexcept
        : Core{State{::new(detail::heap_allocate_(::foundation::alloc::Layout::of<T>()))
                         T(static_cast<Args&&>(args)...)}} {}

    // Builds the object from the arguments in the storage of a Hole of the
    // size and the alignment of T, with no call of the allocation door.  The
    // Hole is then empty, and a Hole that holds no storage ends the process
    // through fatal().  Only mint_box holds the key.  The shape of the Hole
    // is a parameter of this template, so a Box of a declared type names no
    // size of T.
    template <std::size_t Size, std::size_t Align, class... Args>
        requires CompleteBoxPayload<T> && (Size == sizeof(T)) && (Align == alignof(T))
              && std::constructible_from<T, Args&&...>
    explicit Box(detail::BoxKey const& /*key*/, Hole<Size, Align>&& hole, Args&&... args) noexcept
        : Core{State{::new(std::move(hole).detach_()) T(static_cast<Args&&>(args)...)}} {}

    // Takes the storage of the size and the alignment of T from the
    // allocation door, and builds in it the T that build gives as a prvalue.
    // The guaranteed elision builds the object in place, so a T that cannot
    // move and that only a door builds, such as an arena, fits a Box.  Only a
    // mint holds the key.
    template <class Build>
        requires CompleteBoxPayload<T> && std::is_nothrow_invocable_v<Build&&>
              && std::same_as<std::invoke_result_t<Build&&>, T>
    explicit Box(detail::BoxKey const& /*key*/, detail::BoxBuild /*form*/, Build&& build) noexcept
        : Core{State{::new(detail::heap_allocate_(::foundation::alloc::Layout::of<T>()))
                         T(static_cast<Build&&>(build)())}} {}

    // A Box of the object of a word of the authority of the key (the word of
    // a Box).  claim_box calls it.  The empty word ends the process through
    // fatal().  An explicit specialization of this constructor is a
    // specialization of Box outside its home, as above.
    template <class Authority>
    explicit Box(BoxWord<T, Authority> word, box_word_key<Authority> const& /*key*/) noexcept
        : Core{State{claimed_(word.object_)}} {}

    // Gives the object to a word of the authority of the key, and the Box
    // then holds no object (the word of a Box).  A Box that holds no object
    // ends the process through fatal().  A word is not linear, so a Box of a
    // linear payload gives no word.
    template <class Authority>
        requires DroppableBoxPayload<T>
    [[nodiscard]] BoxWord<T, Authority> release_to_word(box_word_key<Authority> const& /*key*/) && noexcept {
        T* const object = held_();
        this->state_.object = nullptr;
        return BoxWord<T, Authority>{object};
    }

    // The assignment ends the object that this Box holds after it takes
    // other, so other can live inside that object.  A linear object must not
    // end that way, so a Box of a linear payload has no assignment.  The two
    // assignments read is_linear_box, the one fact of linearity that the Box
    // fixes where a unit first names it, and they walk no payload.  GCC checks
    // the constraints of each member function again when a later read lists
    // the members of a class, so a walk of the payload here could reach a
    // member read of a node that holds this Box, which is still open.  A Box
    // that a unit names early is thus not linear, and the end of its object
    // stops the build when the payload is linear.
    Box(Box&&) noexcept = default;
    Box& operator=(Box&&) noexcept
        requires(!is_linear_box<T>)
    = default;
    Box& operator=(Box&&)
        requires(is_linear_box<T>)
    = delete("an assignment ends the linear payload that the target Box holds, and a linear value must not be "
             "dropped: consume the target first, or build a new Box");

    Box(Box const&) = delete("a Box owns its object alone: a copy would free the object two times");
    Box& operator=(Box const&) = delete("a Box owns its object alone: a copy would free the object two times");

    [[nodiscard]] T& operator*() & noexcept { return *held_(); }
    [[nodiscard]] T const& operator*() const& noexcept { return *held_(); }
    [[nodiscard]] T* operator->() & noexcept { return held_(); }
    [[nodiscard]] T const* operator->() const& noexcept { return held_(); }

    // The borrow of the whole object as a Ref with the fresh brand of the
    // call.  The Ref has no test, so a loop reads the object through it at
    // the cost of a raw pointer.  The pack refuses each explicit argument, so
    // no caller names the brand.  The result type names the brand through
    // undeducible_brand_t, so the address of view with a target type that
    // names a different brand deduces no brand, and it matches no overload.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] Ref<T, ::foundation::brand::undeducible_brand_t<Fresh>> view() & noexcept {
        return detail::RefDoor::lend<Fresh>(*held_());
    }
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>> view() const& noexcept {
        T const& object = *held_();
        return detail::RefDoor::lend<Fresh>(object);
    }

    // Moves the object out, destroys the moved-from object, and frees the
    // storage.  The Box then holds no object.  The constraint reads T only
    // at a call, because consume is a template.  So an Option of a Box of a
    // declared type can read the members of the Box with no error.
    template <class Payload = T>
        requires std::same_as<Payload, T> && std::is_nothrow_move_constructible_v<Payload>
    [[nodiscard]] T consume() && noexcept {
        if (this->state_.object == nullptr) [[unlikely]] {
            fatal("consume of a Box that holds no object: the Box was moved from or consumed");
        }
        T taken(static_cast<T&&>(*this->state_.object));
        this->reset_();
        return taken;
    }

    void view() && = delete("a borrow of a temporary Box dangles at the end of the full expression");
    void view() const&& = delete("a borrow of a temporary Box dangles at the end of the full expression");
    T& operator*() && = delete("a borrow of a temporary Box dangles at the end of the full expression");
    T const& operator*() const&& = delete("a borrow of a temporary Box dangles at the end of the full expression");
    T* operator->() && = delete("a borrow of a temporary Box dangles at the end of the full expression");
    T const* operator->() const&& = delete("a borrow of a temporary Box dangles at the end of the full expression");
};

// The storage has the size and the alignment of T, and the destructor of
// the Box gives it back with the same two values.  The door never gives a
// null pointer.  An argument list that does not build a T, and a tag that
// is not the allocation capability, fail the gate at the call.
template <class T, class Marker, class... Args>
    requires CanMintBox<T, Marker, Args...>
[[nodiscard]] Box<T> mint_box(Marker /*allocation*/, Args&&... args) noexcept {
    return Box<T>{detail::BoxDoor::key(), static_cast<Args&&>(args)...};
}

// The Box of the object of a word (the word of a Box).  The authority claims
// each word that it made at most one time.
template <BoxPayload T, class Authority>
[[nodiscard]] Box<T> claim_box(BoxWord<T, Authority> word, box_word_key<Authority> const& key) noexcept {
    return Box<T>{word, key};
}

// Frame of a Box
//   with_mut(std::move(box), body) is the frame door of a Box, in the form of
//   the doors of the owners on the heap (foundation/core/Frame.h).  The Box
//   moves into the frame of the door, body gets the object as T& for the time
//   of the call, and the door gives the Box back: alone for a body that gives
//   void, and after the result in a Record for each other body.  While the
//   body runs, the caller holds no name for the object: the name that it
//   moved from holds no object.  So no code ends the object, moves it, or
//   gives its storage to a Hole while the body holds the reference, also in a
//   function that the body calls.
//
//   The door carries the annotation borrow_match, so the rule borrow-match
//   refuses a body that assigns or moves the name of the caller, such as a
//   mint_hole of that name or a move of it into a closure.  The result of the
//   body holds no borrow, so no reference to the object leaves the frame.  A
//   Box with no object ends the process at the door.  In the Release code, a
//   body that reads the object costs what operator-> costs, and a body that
//   writes it adds one store of the pointer of the Box: the move back.

// A callable that takes the object of a Box of T as T& with no throw, and
// that gives void or a value that holds no borrow and fits a Record, so the
// value leaves the frame with the Box.
template <class Body, class T>
concept BoxFrameCallable = std::is_nothrow_invocable_v<Body&&, T&>
                        && (std::is_void_v<detail::body_result_t<Body&&, T&>>
                            || (HoldsNoBorrow<detail::body_result_t<Body&&, T&>>
                                && RecordElement<std::remove_cv_t<detail::body_result_t<Body&&, T&>>>));

// The body of the frame door of a Box of T: a callable of BoxFrameCallable
// that holds no linear value by value (HoldsNoLinearValue of
// foundation/Linear.h).
template <class Body, class T>
concept BoxFrameBody =
    BoxFrameCallable<Body, T> && ::foundation::linear::HoldsNoLinearValue<Body> && StoresNoBorrowOutside<Body>;

// A body whose result does not leave the frame: the refused twin of the door
// binds to it.
template <class Body, class T>
concept RefusedBoxFrameBody = std::is_nothrow_invocable_v<Body&&, T&> && (!BoxFrameCallable<Body, T>)
                           && ::foundation::linear::HoldsNoLinearValue<Body>;

// A body of a door of a Box of T that takes Object, T& or T const&, and that
// holds a linear value by value: the linear twin of each door binds to it.
template <class Body, class Object>
concept LinearBoxBody = (!::foundation::linear::HoldsNoLinearValue<Body>) && std::is_invocable_v<Body&&, Object>;

// The type that the frame door of a Box of T gives back for a body.
template <class Body, class T>
using box_frame_return_t = detail::frame_return_t<detail::body_result_t<Body&&, T&>, Box<T>>;

// The frame door of a Box (the definition below).  BoxFrameKeyDoor names it
// as a friend, so this declaration stands before the door.
template <BoxPayload T, class Body>
    requires DroppableBoxPayload<T> && BoxFrameBody<Body, T>
[[nodiscard]] box_frame_return_t<Body, T> with_mut(Box<T>&& box, Body&& body) noexcept;

// The door of the frame door of a Box to the key of the frame doors (the
// doors of the owners above foundation/core/Frame.h).  Its one member is
// private, and the frame door is its one friend.  The invariant: the frame
// door reads the object of the Box that moves into its frame, and the Box
// owns that object alone.  A home owner row of utils/rule-table.txt keeps
// this definition in this header.
class BoxFrameKeyDoor final : ::foundation::NoObject<BoxFrameKeyDoor> {
    template <BoxPayload T, class Body>
        requires DroppableBoxPayload<T> && BoxFrameBody<Body, T>
    friend auto ::foundation::core::with_mut(Box<T>&& box, Body&& body) noexcept -> box_frame_return_t<Body, T>;

    // The key of the frame doors, for the object of a Box in the frame.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

// The frame door of a Box (the frame of a Box).  Complexity: O(1), and the
// cost of the body.
template <BoxPayload T, class Body>
    requires DroppableBoxPayload<T> && BoxFrameBody<Body, T>
[[= ::foundation::linear::borrow_match{}]] [[nodiscard]] box_frame_return_t<Body, T> with_mut(Box<T>&& box,
                                                                                              Body&& body) noexcept {
    using Given = detail::body_result_t<Body&&, T&>;
    // The test of the object comes before the move, so no fatal path stands
    // between the move in and the move out, and the optimizer can drop the
    // store of the moved-from Box when the body does not read it.
    T& object = frame_object_(BoxFrameKeyDoor::key_(), box);
    Box<T> held{std::move(box)};
    if constexpr (std::is_void_v<Given>) {
        static_cast<Body&&>(body)(object);
        return held;
    } else {
        return detail::frame_return_t<Given, Box<T>>{static_cast<Body&&>(body)(object), std::move(held)};
    }
}

// A body whose result holds a borrow binds to this overload, so the refusal
// gives its reason.
template <BoxPayload T, class Body>
    requires DroppableBoxPayload<T> && RefusedBoxFrameBody<Body, T>
void with_mut(Box<T>&& box, Body&& body) =
    delete("the result of a frame door leaves the frame, so it holds no reference and no address: give a plain value");

template <BoxPayload T, class Body>
    requires DroppableBoxPayload<T> && LinearBoxBody<Body, T&>
void with_mut(Box<T>&& box, Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// A linear object leaves a Box only through consume.  A body could move it
// out through the reference and leave the Box full of a moved-from value,
// and a later consume would give that value out again.  So a Box of a linear
// payload has no frame door, as an Option of a linear payload has none.
template <BoxPayload T, class Body>
    requires CompleteBoxPayload<T> && (!DroppableBoxPayload<T>)
void with_mut(Box<T>&& box, Body&& body) =
    delete("a Box of a linear payload has no frame door, because the body could move the object out and leave the Box "
           "full: take the object out with consume");

// A named Box stays with the caller while the body runs, so the body could
// end its object beside the reference through a capture.  This twin refuses
// a named Box and a const one, which cannot move into the frame.
template <class Owner, class Body>
    requires(std::is_lvalue_reference_v<Owner> || std::is_const_v<std::remove_reference_t<Owner>>)
             && ::foundation::reflect::IsInstanceOf<std::remove_cvref_t<Owner>, ^^Box>
void with_mut(Owner&& box, Body&& body) = delete(
    "with_mut takes the Box into its frame and gives it back: pass std::move(box), and keep the Box that the call "
    "returns");

// Read frame of a Box
//   with_view(box, body) lends the object of a Box to body as T const& in
//   place, for the time of the call, and gives the result of body.  The Box
//   stays with the caller, and the door writes no byte of it and adds none to
//   it.  The door carries the annotation borrow_match, so the rule
//   borrow-match refuses a body that assigns, moves or consumes the name of
//   the Box, such as a consume or a mint_hole of it.  The result of the body
//   holds no borrow, so no reference to the object outlives the call, and a
//   later move or end of the Box ends no object that a borrow names.  A Box
//   with no object ends the process at the door, as operator* does.  A
//   function that the body calls can still reach the Box, as it can reach an
//   inline owner in the frame of its door (the two forms of an owner of
//   foundation/core/Frame.h).  A body cannot move a linear payload out of a
//   const reference, so the door takes a Box of each payload.

// A callable that takes the object of a Box of T as T const& with no throw,
// and that gives void or a value that holds no borrow.
template <class Body, class T>
concept BoxReadCallable = std::is_nothrow_invocable_v<Body&&, T const&>
                       && (std::is_void_v<detail::body_result_t<Body&&, T const&>>
                           || HoldsNoBorrow<detail::body_result_t<Body&&, T const&>>);

// The body of the read frame of a Box of T: a callable of BoxReadCallable
// that holds no linear value by value.
template <class Body, class T>
concept BoxReadBody =
    BoxReadCallable<Body, T> && ::foundation::linear::HoldsNoLinearValue<Body> && StoresNoBorrowOutside<Body>;

// The read frame of a Box (the read frame of a Box).  The result holds no
// borrow, so the deduced type of a value is the result of the body.
// Complexity: O(1), and the cost of the body.
template <BoxPayload T, class Body>
    requires CompleteBoxPayload<T> && BoxReadBody<Body, T>
[[= ::foundation::linear::borrow_match{}]] auto with_view(Box<T> const& box, Body&& body) noexcept {
    return static_cast<Body&&>(body)(*box);
}

// A body whose result holds a borrow binds to this overload, so the refusal
// gives its reason.
template <BoxPayload T, class Body>
    requires CompleteBoxPayload<T> && std::is_nothrow_invocable_v<Body&&, T const&> && (!BoxReadCallable<Body, T>)
              && ::foundation::linear::HoldsNoLinearValue<Body>
void with_view(Box<T> const& box, Body&& body) =
    delete("the result of the read frame of a Box holds no reference and no address, because a later move or end of "
           "the Box ends the object: give a plain value");

template <BoxPayload T, class Body>
    requires CompleteBoxPayload<T> && LinearBoxBody<Body, T const&>
void with_view(Box<T> const& box, Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// Size bytes of heap storage at the alignment Align, with no object in it.
// The annotation no_start_over_bytes refuses a Hole to the checked lifetime
// start, so no bytes become a Hole that frees storage that it does not own.
template <std::size_t Size, std::size_t Align>
    requires HoleShape<Size, Align>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] Hole final
    : public OwnerCore<detail::owner::HoleState<Size, Align>, Hole<Size, Align>> {
    using State = detail::owner::HoleState<Size, Align>;
    using Core = OwnerCore<State, Hole>;

    template <BoxPayload T>
    friend class Box;

    // The storage of a Hole that a path spends.  A moved-from or spent Hole
    // holds none, and each build tests it here.
    [[nodiscard]] void* held_() const noexcept {
        if (this->state_.storage == nullptr) [[unlikely]] {
            fatal("a spend of a Hole that holds no storage: the Hole was moved from or spent");
        }
        return this->state_.storage;
    }

    // Gives the storage to a Box, which builds its object in it.  The Hole
    // is then empty.
    [[nodiscard]][[= ::foundation::linear::detach_door{}]] void* detach_() && noexcept {
        void* const storage = held_();
        this->state_.storage = nullptr;
        return storage;
    }

public:
    // Takes Size bytes at the alignment Align from the allocation door.
    // Only mint_hole holds the key.
    explicit Hole(detail::HoleKey const& /*key*/) noexcept
        : Core{State{detail::heap_allocate_(detail::hole_layout_<Size, Align>)}} {}

    // Ends the object of a Box of the exact size and alignment, and takes
    // its storage.  The Box is then empty.  Only mint_hole holds the key.
    template <BoxPayload T>
        requires(sizeof(T) == Size && alignof(T) == Align)
    explicit Hole(detail::HoleKey const& /*key*/, Box<T>&& box) noexcept : Core{State{box.vacate_()}} {}

    // The core of foundation/core/Owner.h gives the moves and the end of a
    // Hole.
    Hole(Hole&&) noexcept = default;
    Hole& operator=(Hole&&) noexcept = default;

    Hole(Hole const&) = delete("a Hole owns its storage alone: a copy would free the storage two times");
    Hole& operator=(Hole const&) = delete("a Hole owns its storage alone: a copy would free the storage two times");

    // Gives the storage back to the allocation door.  The Hole is then
    // empty.
    [[= ::foundation::linear::drop_door{}]] void consume() && noexcept {
        static_cast<void>(held_());
        this->reset_();
    }
};

// The gate of the mint of a Hole from the allocation capability.
template <std::size_t Size, std::size_t Align, class Marker>
concept CanMintHole = HoleShape<Size, Align> && AllocationCapability<Marker>;

// A Hole of new storage from the allocation door.  The door never gives a
// null pointer.
template <std::size_t Size, std::size_t Align, class Marker>
    requires CanMintHole<Size, Align, Marker>
[[nodiscard]] Hole<Size, Align> mint_hole(Marker /*allocation*/) noexcept {
    return Hole<Size, Align>{detail::HoleDoor::key()};
}

// A Hole of the storage of a Box that the caller gives up.  The object of
// the Box ends, and its storage stays with no call of the door.  A Box that
// holds no object ends the process through fatal().  The end of a linear
// object drops it, so a Box of a linear payload gives no Hole: consume the
// Box, and take a Hole of new storage.
template <DroppableBoxPayload T>
[[nodiscard]] Hole<sizeof(T), alignof(T)> mint_hole(Box<T>&& box) noexcept {
    return Hole<sizeof(T), alignof(T)>{detail::HoleDoor::key(), std::move(box)};
}

// The gate of the mint of a Box in a Hole: the Hole has the exact size and
// alignment of T, so the Box gives back the storage that the Hole holds.
template <class T, std::size_t Size, std::size_t Align, class... Args>
concept CanMintBoxInHole =
    CompleteBoxPayload<T> && Size == sizeof(T) && Align == alignof(T) && std::constructible_from<T, Args&&...>;

// A Box of the object that the arguments build in the storage of a Hole
// that the caller gives up.  The mint calls no allocation door, so it takes
// no allocation capability: the mint of the Hole took it.
template <class T, std::size_t Size, std::size_t Align, class... Args>
    requires CanMintBoxInHole<T, Size, Align, Args...>
[[nodiscard]] Box<T> mint_box(Hole<Size, Align>&& hole, Args&&... args) noexcept {
    return Box<T>{detail::BoxDoor::key(), std::move(hole), static_cast<Args&&>(args)...};
}

}  // namespace foundation::core
