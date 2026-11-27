#pragma once

// The arrays of the Region family: an owner of exactly N elements, and an
// owner of a count of elements on the heap that its mint fixes.
//
//   FixedArray<T, N>    N elements inline, in the object itself
//   HeapArray<T, Align> count elements on the heap, at the alignment Align
//   MaskedArray<T, Align>
//                       a power-of-two count of elements on the heap, read
//                       at a masked key with no check
//
// A FixedArray fills each element at its construction.  The default
// constructor value-initializes each element, so an array of integers holds
// zeros, and the constructor with N arguments initializes each element from
// its argument.  A list of fewer than N arguments is refused, so no tail of
// an array is left with a value that no code chose.
//
// Reads
//   size()              N, a constant
//   view()              the View<T const, N> of the elements: the door to a
//                       View for code outside the base.  A View of a
//                       temporary array is refused, because it would dangle
//                       at the end of the full expression
//   for (x : array)     each element, in order, as T const&
//   at(index)           the element at an Index<N> or at index<I>(), with no
//                       check: the type of the index proves the bound
//   indices()           each Index<N>, in order
//   enumerate()         each element with its Index<N>, in order
//
// A FixedArray has no operator[], no data() and no pointer to its
// elements, so no read takes an index that no check proved.  An index from
// data goes through Index<N>::check(raw), which gives an Option.  Each read
// gives a const element.  The value doors write the elements with no body,
// with_element of a keyed array writes one element in a body (the element
// door), and fill and copy of the Region family write the elements through
// the ViewMut of with_mut of a keyed array (the frame door) or of
// build_fixed_array (the build door).  So no two Views of one array write the
// same elements.
//
// A FixedArray copies and moves its elements, and it is trivially copyable
// when its element is, so the ABI passes a small array in registers.
//
// The end of a FixedArray ends its elements, from the last.  The count is in the
// type, so no state tells that an element ended, as in a C array.  The end of the
// array is the end of its scope, and the kind stale-borrow of the rule
// view-escape refuses a later use by name.  Only the code of an element can come
// in between (the code of an element of foundation/core/Owner.h), and a read of
// the array by it through a second name reads an ended element.
//
// Value doors
//   array.set(index, value)   the element at an Index<N> becomes value
//   array.exchange(index, value)
//                       the element becomes value, and the door gives the
//                       value that the element held
//   array.update(index, body)
//                       body changes a copy of the element, and the door
//                       then puts the copy in the place of the element and
//                       gives the result of body (the update door)
//   array.build_at(index, fn, args...)
//                       the element becomes the value of fn(args...), built
//                       in its place
//   array.fill(value)   each element becomes value
//   array.copy_from(view)
//                       each element becomes the element of a
//                       View<T const, N> at its position
//   array.copy_window(start, view)
//                       the elements from the position start become the
//                       elements of a View.  A View that does not fit gives
//                       LengthMismatch, and the door then writes nothing
// A value door lends no element and no run.  Its value comes to it as an
// argument, so no code of the caller runs while the door writes, and no second
// door of the array opens inside it.  So a value door takes no key.  The body
// of update runs while no door writes: it gets a copy of the element, and no
// reference of the door names the storage of the array.  A copy
// gives the array the values that the source held before the copy, also when
// the source is a View of the same array.  A set ends the old value of the
// element, so an element that a holder cannot drop, such as a linear value,
// takes exchange, which gives the old value back.
//
// A keyed array (the loan key of foundation/core/Frame.h) has no value door:
// another name of the array could write an element while a frame of the array
// is open.  It writes one element through with_element(key, index, body).
//
// Build door
//   build_fixed_array<T, N>(body)
//                       a new array of N value-initialized elements: body gets
//                       the ViewMut<T, Fresh, N> of the elements, and the door
//                       gives the array, alone for a body that gives void, and
//                       after the result in a Record for each other body.  The
//                       array has no name until the door gives it, so no code
//                       opens a second frame of it, and the door takes no key.
//                       The door refuses a body that could write the object
//                       that the array initializes, such as the name of the
//                       variable in its own initializer
//
// Heap array
//   The five mints put count elements in storage from the allocation door of
//   foundation/core/Heap.h.  The allocation capability comes first in each
//   mint, so each allocation shows at its call.  A count of zero allocates
//   nothing.  Each element holds a value before the mint returns.
//
//   mint_heap_array(alloc, count, fill)
//                       count copies of fill
//   mint_heap_array_with<T>(alloc, count, init)
//                       the element at each position built in place from
//                       init(position).  No element moves, so a pinned
//                       element, such as an Atomic, passes
//   mint_heap_array_copy<T>(alloc, source)
//                       a copy of each element of a View
//   mint_heap_array_zeroed<T>(alloc, count)
//                       count elements with zero in each byte.  Each bit
//                       pattern of the element is a value (BitValid)
//   mint_heap_array_written<T>(alloc, count, writer)
//                       count elements of an implicit-lifetime type that
//                       writer(lease) writes through a WriteLease.  The lease
//                       writes the elements in order and has no read, so no
//                       code reads an element before its write.  A writer
//                       that leaves an element with no write, or that gives
//                       an element past the last one, ends the process before
//                       the array exists
//
//   size(), view(), the loop, get(position), and the iteration verbs, as
//                       a FixedArray gives them: each gives const elements.
//                       No type holds the count of a HeapArray, so it has no
//                       indices() and no enumerate()
//   with_element(position, on_some, on_none)
//                       the element door: on_some gets the element at a
//                       position below size() in place, and on_none runs for
//                       each other position (the element door)
//
// A write of one element takes with_element, and a write of the run takes
// the frame door with_mut (the frame door).  So no two Views of one array
// write the same elements.
//
// A HeapArray owns its storage alone.  A move gives the storage to the
// target and leaves the source empty, and the destructor ends each element
// and gives the storage back.  An empty HeapArray, also the default one,
// holds no storage.  A HeapArray of a linear element is linear itself, so
// each path of its holder consumes it.
//
// Masked array
//   mint_masked_array<T, Align>(alloc, count)   the one door: count
//                       elements from their default constructor, so an
//                       element that does not copy passes.  The count is a
//                       power of two from 1 to 2^31
//   size(), view()      as a HeapArray gives them: const elements
//   at_masked(key)      the const element at key & (count - 1), with no check
//   with_masked(key, body)
//                       the element door: body gets the element at
//                       key & (count - 1) in place (the element door)
//
// A MaskedArray owns its storage as a HeapArray does.  It has no operator[]
// and no pointer to its elements.  A write of one element takes
// with_masked, and a write of the run takes the frame door with_mut.
//
// Frame door
//   array.with_mut(key, body)   of a keyed FixedArray: body gets the ViewMut
//                       of the elements in place, with the brand of the
//                       frame, and the door gives the result of body.  The
//                       door writes no byte of the array and copies no
//                       element.  The key comes by a borrow or by move
//   with_mut(std::move(array), body)   of a HeapArray and of a MaskedArray:
//                       the array moves into the frame, body gets the ViewMut
//                       of the elements, and the door gives the array back.
//                       The move takes a pointer and a count
// foundation/core/Frame.h gives the rules of the two forms.
//   mint_keyed_owner(fixed_array_owner<T, N>)   a keyed FixedArray and its
//                       LoanKey of the fresh brand of the call
//                       (foundation/core/KeyedMint.h)
// A FixedArray with no key opens no frame.  Another name of it, such as a
// capture of the body or a Ref, would open a second frame with a second brand,
// and the two frames would write one run.
//
// Element door
//   array.with_element(key, index, body)   of a keyed FixedArray, with an
//                       Index<N>: body gets the element in place, as T&, for
//                       the time of the call, and the door gives the result
//                       of body
//   array.with_element(index, body)   of a FixedArray with no key: body gets
//                       the element in place, as T&, when two threads can
//                       write one element at the same time (Synchronized of
//                       foundation/Transfer.h), and as T const& in each other
//                       case.  A body that writes an element of each other
//                       type takes update, which gives it a copy
//   with_element(position, on_some, on_none)   of a HeapArray, with a
//                       position that no type proves: on_some gets the
//                       element in place below size(), and on_none runs at or
//                       past it.  The two arms give one type.  One compare
//   with_masked(key, body)   of a MaskedArray: body gets the element at
//                       key & (count - 1) in place.  The mask keeps each key
//                       below the count, so the door has no none arm
// The result holds no borrow (HoldsNoBorrow of foundation/core/Choice.h), so
// no reference to the element leaves the door, and the door gives no View.  A
// const array gives T const&.  The door with no key carries the annotation
// borrow_match, so the rule borrow-match refuses a call of a member of the
// same array that is not const in the body, such as a second door that would
// give a second reference to the element.  A body that writes the whole run
// takes with_mut.
//
// The door writes no state of the array.  So two threads can call it on one
// shared array at the same time, and only an element that is safe for
// concurrent writes, such as an Atomic or a pinned cell, makes such calls
// correct.
//
// Lifetime start
//   lifetime::start_as_array<T>(storage, count)
//                       the View of count objects of T whose lifetime starts
//                       over the bytes at storage, with no constructor.  T is
//                       an implicit-lifetime type throughout, the concept of
//                       foundation/Lifetime.h.  This header includes that
//                       header and the View, so the door stands here
//   lifetime::start_as_array<T>(bytes)
//                       the same door over the bytes of a View of unsigned
//                       char or std::byte: the View of the objects of T that
//                       fill the bytes, with no raw address at the call

#include <foundation/Brand.h>
#include <foundation/Lifetime.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Index.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/reflect/Instance.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace foundation::core {

// The element of an owner of the Region family: an object type that is not
// an array and not cv-qualified, whose destructor does not throw.  A const
// owner gives const elements.
template <class T>
concept OwnedElement = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                    && std::is_nothrow_destructible_v<T>;

namespace detail {

// The alignment of the storage of a HeapArray: a power of two, and at least
// the alignment of the element.
template <class T, std::size_t Align>
concept HeapArrayAlignment = Align != 0 && (Align & (Align - 1)) == 0 && Align >= alignof(T);

}  // namespace detail

// The two arrays of this header.  ArrayRefDoor names them as friends, so
// these declarations stand before the door, and the definitions below give
// the default arguments.
template <class T, std::size_t N, ::foundation::brand::IsBrand KeyBrand>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N>
class FixedArray;
template <class T, std::size_t Align>
    requires OwnedElement<T> && detail::HeapArrayAlignment<T, Align>
class HeapArray;

// The parts of a keyed holder of foundation/core/KeyedArray.h: an element of
// a heap array that a holder can drop, an alignment of the storage, and a key
// brand.
template <class T, class Brand, std::size_t Align>
concept KeyedHolderParts =
    OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && IsKeyBrand<Brand> && DroppablePayload<T>;

// The keyed holder of a HeapArray (foundation/core/KeyedArray.h).  HeapArray
// names it as a friend, so this declaration stands before HeapArray and gives
// the default argument.
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Align = alignof(T)>
    requires KeyedHolderParts<T, Brand, Align>
class KeyedHeapArray;

// The door of a FixedArray and a HeapArray to the body of each get of a run
// and to the root stage of enumerate() (RegionRefDoor of
// foundation/core/Region.h).  Its members are private, and the two arrays are
// its friends.  The invariant: a FixedArray holds N live elements from its
// first element, and a HeapArray holds the count of its state from the first
// element of its state, so get() names the first element and the count of
// live elements.  enumerate() of a FixedArray gives the View of its N
// elements and the fresh brand of its call.  A home owner row of
// utils/rule-table.txt keeps this definition in this header.
class ArrayRefDoor final : ::foundation::NoObject<ArrayRefDoor> {
    template <class T, std::size_t N, ::foundation::brand::IsBrand KeyBrand>
        requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N>
    friend class FixedArray;
    template <class T, std::size_t Align>
        requires OwnedElement<T> && detail::HeapArrayAlignment<T, Align>
    friend class HeapArray;

    // The element at position of the count live elements from first, as an
    // Option of a Ref of the brand, or no value at or past the count.
    template <::foundation::brand::IsBrand Brand, class T>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr auto element_at_(T* first, std::size_t count,
                                                                    std::size_t position) noexcept {
        return RegionRefDoor::element_at_<Brand>(first, count, position);
    }

    // The root stage of enumerate() over each element of the View of an array,
    // with the brand of the call of the verb.
    template <::foundation::brand::IsBrand Brand, class Element, std::size_t Count>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr auto enumerate_stage_(View<Element, Count> elements) noexcept {
        return RegionRefDoor::enumerate_stage_<Brand>(elements);
    }
};

// The array of a mask of this header.  ArrayFrameOwner names it, so this
// declaration stands before the concept, and the definition below gives the
// default argument.
template <class T, std::size_t Align>
    requires OwnedElement<T> && detail::HeapArrayAlignment<T, Align>
class MaskedArray;

// The arrays that the free frame door takes (foundation/core/Frame.h).  Each
// owns its elements on the heap alone, and a move gives the storage to the
// target with no copy of an element.  A FixedArray holds its elements inline,
// so a move would copy them: its frame door is the member with_mut with the key
// of the array, which lends the elements in place.
template <class Owner>
concept ArrayFrameOwner = ::foundation::reflect::IsInstanceOfAny<Owner, ^^HeapArray, ^^MaskedArray>;

namespace detail {

// The extent of the run of an array in a frame door: the extent of its
// view(), the dynamic extent for each array of the free door.
template <class Owner>
inline constexpr std::size_t frame_extent_v = decltype(std::declval<Owner const&>().view())::extent;

}  // namespace detail

// The free frame door of a heap array (the definition below).
// ArrayFrameKeyDoor names it as a friend, so this declaration stands before
// the door, and the default argument of the brand belongs to it.
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires NamesNoArgument<Refused...> && MovedOwner<Owner> && ArrayFrameOwner<Owner>
          && FrameBody<Body, typename Owner::element_type, Fresh, detail::frame_extent_v<Owner>>
[[nodiscard]] constexpr detail::frame_return_t<
    detail::body_result_t<Body&&, ViewMut<typename Owner::element_type, Fresh, detail::frame_extent_v<Owner>>>, Owner>
with_mut(Owner&& owner, Body&& body) noexcept;

namespace detail {

// True when an object of the node holds the array by value: the node itself,
// and each base, each member and each element of an array that it holds by
// value, at each depth.  A class that the unit does not complete holds no
// array that the unit knows.  It is a function that is no template, so no
// unit can specialize what it admits.  Complexity: linear in the parts that
// the node holds by value, at compile time only.
[[nodiscard]] consteval bool holds_array_by_value_(::foundation::reflect::TypeNode root, std::meta::info array) {
    namespace refl = ::foundation::reflect;
    constexpr refl::SpecializationRead read = refl::SpecializationRead::Instantiating;
    refl::TypeNodeList listed;
    listed.push_back(root);
    for (std::size_t next = 0; next < listed.size(); ++next) {
        refl::TypeNode const node = listed.begin()[next];
        if (node.type == array) return true;
        if (std::meta::is_array_type(node.type)) {
            for (refl::TypeNode const& element : refl::argument_components_of(node, read))
                listed.push_back(element);
            continue;
        }
        for (refl::TypeNode const& part : refl::member_components_of(node, read)) {
            if (part.is_held_by_value) listed.push_back(part);
        }
    }
    return false;
}

// True when a value of the body could write an object that holds the array: a
// part that the body holds by value, at each depth, is a reference or a
// pointer to an object that is not const, or a Ref or a View of one, and that
// object holds the array by value.  The target of a borrow is one step away.
// An object whose address is older than the array holds no address of it.  It
// is a function that is no template, so no unit can specialize what it admits.
// Complexity: linear in the parts of the body and of each target, at compile
// time only.
[[nodiscard]] consteval bool could_write_array_(std::meta::info body, std::meta::info array) {
    namespace refl = ::foundation::reflect;
    constexpr refl::SpecializationRead read = refl::SpecializationRead::Instantiating;
    refl::TypeNodeList held;
    held.push_back(refl::TypeNode{refl::bare_type(body), true});
    for (std::size_t next = 0; next < held.size(); ++next) {
        for (refl::TypeNode const& part : refl::member_components_of(held.begin()[next], read)) {
            bool const is_borrow =
                std::meta::is_pointer_type(part.type)
                || (std::meta::has_template_arguments(part.type)
                    && (std::meta::template_of(part.type) == ^^View || std::meta::template_of(part.type) == ^^Ref));
            if (part.was_reference) {
                if (!part.was_const && holds_array_by_value_(part, array)) return true;
            } else if (is_borrow) {
                refl::TypeNode const target =
                    std::meta::is_pointer_type(part.type)
                        ? refl::argument_components_of(part, read).begin()[0]
                        : ::foundation::transfer::detail::borrow_target_of(refl::bare_type(part.type)).node;
                if (!target.was_const && holds_array_by_value_(target, array)) return true;
            } else {
                held.push_back(part);
            }
        }
    }
    return false;
}

}  // namespace detail

// The element of an array that build_fixed_array builds: an element of a
// FixedArray of N with a default constructor that does not throw.
template <class T, std::size_t N>
concept BuildableElement =
    OwnedElement<T> && IndexCount<N> && ViewExtent<T, N> && std::is_nothrow_default_constructible_v<T>;

// A body of build_fixed_array that could write no object that holds an array
// of the type that the door builds (could_write_array_).
template <class Body, class T, std::size_t N>
concept BuildsUnnamedArray =
    BuildableElement<T, N>
    && !detail::could_write_array_(^^std::remove_reference_t<Body>, std::meta::dealias(^^FixedArray<T, N, Unkeyed>));

// The gates of a body of build_fixed_array, which read no call of the body: the
// gates of each frame body, and no object that the array could initialize.
template <class Body, class T, std::size_t N>
concept BuildGates = BuildsUnnamedArray<Body, T, N> && FrameGates<Body>;

// The body of build_fixed_array: a frame callable that passes the gates.
template <class Body, class T, std::size_t N, class Fresh>
concept BuildsFixedArray = BuildGates<Body, T, N> && FrameCallable<Body, T, Fresh, N>;

// The build door of a FixedArray (the definition below).  ArrayFrameKeyDoor
// names it as a friend, so this declaration stands before the door, and the
// default argument of the brand belongs to it.
template <class T, std::size_t N, class... Refused, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires NamesNoArgument<Refused...> && BuildsFixedArray<Body, T, N, Fresh>
[[nodiscard]] constexpr detail::frame_return_t<detail::body_result_t<Body&&, ViewMut<T, Fresh, N>>,
                                               FixedArray<T, N, Unkeyed>>
build_fixed_array(Body&& body) noexcept;

// The door of the arrays of this header to the key of the frame doors (the
// doors of the owners above foundation/core/Frame.h).  Its one member is
// private.  The invariant: a FixedArray lends its own elements in place, in
// the frame of a door of the array, with the scope brand of the call of the
// door.  The free frame door holds a HeapArray or a MaskedArray by move in its
// frame, and lends its run with the scope brand of its call.  The build door
// lends the run of an array that it holds as a local with no other name, with
// the scope brand of its call.  FixedArray, the free frame door and the build
// door are its friends.  A home owner row of utils/rule-table.txt keeps this
// definition in this header.
class ArrayFrameKeyDoor final : ::foundation::NoObject<ArrayFrameKeyDoor> {
    template <class T, std::size_t N, ::foundation::brand::IsBrand KeyBrand>
        requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N>
    friend class FixedArray;
    template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh>
        requires NamesNoArgument<Refused...> && MovedOwner<Owner> && ArrayFrameOwner<Owner>
              && FrameBody<Body, typename Owner::element_type, Fresh, detail::frame_extent_v<Owner>>
    friend constexpr auto ::foundation::core::with_mut(Owner&& owner, Body&& body) noexcept -> detail::frame_return_t<
        detail::body_result_t<Body&&, ViewMut<typename Owner::element_type, Fresh, detail::frame_extent_v<Owner>>>,
        Owner>;
    template <class T, std::size_t N, class... Refused, class Body, ::foundation::brand::IsBrand Fresh>
        requires NamesNoArgument<Refused...> && BuildsFixedArray<Body, T, N, Fresh>
    friend constexpr auto ::foundation::core::build_fixed_array(Body&& body) noexcept
        -> detail::frame_return_t<detail::body_result_t<Body&&, ViewMut<T, Fresh, N>>, FixedArray<T, N, Unkeyed>>;

    // The key of the frame doors, for the run of an array that a friend holds.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

namespace detail {

// The type of the parameter at position Position of the list constructor of
// an array: T at each position.
template <class T, std::size_t Position>
using ArrayParameter = T;

// The largest count of an array that has the list constructor.  The
// constructor declares one parameter for each element, and the compiler
// builds that declaration for each array type.  An array of 65536 elements
// then costs more than a second of compile time and 100 MB in each unit that
// names it.  No code writes a list of more than 256 values.
inline constexpr std::size_t max_listed_array_count = 256;

template <class T, std::size_t N, class Positions = std::make_index_sequence<(N <= max_listed_array_count) ? N : 0>>
class ArrayStorage;

// The storage of a FixedArray, and its constructors.  The list constructor
// has exactly N parameters of the type T, so a literal argument converts as
// a function argument does, and no tail of the array is left out.  An array
// of more than max_listed_array_count elements has no list constructor.
template <class T, std::size_t N, std::size_t... Positions>
class ArrayStorage<T, N, std::index_sequence<Positions...>> {
protected:
    // Value-initialized for the default constructor.  The list constructor
    // initializes each element itself.
    T elements_[N]{};

    // Each element value-initialized: an integer is zero.  The default
    // constructor of FixedArray holds the constraint.  A constraint here is
    // evaluated when the class of the array is complete.  For an element that
    // is a nested class with default member initializers, inside its
    // enclosing class, that point comes before the compiler reads the
    // initializers.  The trait of the element then gives false, and the
    // compiler keeps that answer for the rest of the unit.  The constructor
    // is protected, so the inherited form of it is no default constructor
    // of a FixedArray whose element has none.
    constexpr ArrayStorage() noexcept = default;

public:
    // Each element from its argument, in order.  The constructor of one
    // argument is explicit, so a value of T does not convert to an array of
    // one element.  The bound comes first in the constraint, so an array
    // past the bound reads no trait of its element.
    constexpr explicit(N == 1) ArrayStorage(ArrayParameter<T, Positions>... values) noexcept
        requires(N <= max_listed_array_count) && std::is_nothrow_move_constructible_v<T>
        : elements_{static_cast<T&&>(values)...} {}
};

// The storage of a keyed FixedArray (the loan key of
// foundation/core/Frame.h).  The body of a frame of the array can still reach
// the array by a name, and only its writers need the key.  So a keyed array
// has no assignment, and it moves only when a move reads its elements and
// writes none of them: a write through the name would change the elements
// under the ViewMut of the frame.  A copy only reads the elements.
template <class T, std::size_t N>
class KeyedArrayStorage : public ArrayStorage<T, N> {
protected:
    constexpr KeyedArrayStorage() noexcept = default;

public:
    using ArrayStorage<T, N>::ArrayStorage;

    KeyedArrayStorage(KeyedArrayStorage const&) = default;
    KeyedArrayStorage(KeyedArrayStorage&&)
        requires std::is_trivially_copyable_v<T>
    = default;
    KeyedArrayStorage(KeyedArrayStorage&&)
        requires(!std::is_trivially_copyable_v<T>)
    = delete("a move of a keyed array writes its elements, and a frame of the array could be open: build the array in "
             "its place");
    KeyedArrayStorage& operator=(KeyedArrayStorage const&) = delete(
        "an assignment of a keyed array writes its elements with no LoanKey, and a frame of the array could be "
        "open: write them through with_mut(key, body)");
    KeyedArrayStorage& operator=(KeyedArrayStorage&&) = delete(
        "an assignment of a keyed array writes its elements with no LoanKey, and a frame of the array could be "
        "open: write them through with_mut(key, body)");
    ~KeyedArrayStorage() = default;
};

// The storage of a FixedArray of the brand.
template <class T, std::size_t N, class KeyBrand>
using array_storage_t = std::conditional_t<IsKeyBrand<KeyBrand>, KeyedArrayStorage<T, N>, ArrayStorage<T, N>>;

}  // namespace detail

// The gates of a body of a keyed frame door, which read the captures of the
// body and no call of it: the gates of each frame body, and no key of the brand
// of the owner that the body reaches (the gate of foundation/core/Frame.h).
// The elements T hold no key of the brand.
template <class Body, class T, class Brand>
concept KeyedFrameGates = ReachesNoLoanKey<Body, Brand> && ReachesNoLoanKey<T, Brand> && FrameGates<Body>;

// The body of a keyed frame door of an owner whose elements are T and whose
// brand is Brand: a frame callable that passes the gates.  The refused twin of
// a keyed door states KeyedFrameGates alone, as each refused twin does
// (FrameBody of foundation/core/Region.h).
template <class Body, class T, class Brand, class Fresh, std::size_t Extent>
concept KeyedFrameBody = KeyedFrameGates<Body, T, Brand> && FrameCallable<Body, T, Fresh, Extent>;

// A body of a keyed frame door that reaches a key of the brand of its owner:
// the refused twin of each keyed door binds to it, also when the body holds
// the key by value.  The test of the key comes first, so the twin reads no
// call of an admitted body.
template <class Body, class T, class Brand, class Fresh, std::size_t Extent>
concept KeyReachingFrameBody = (!ReachesNoLoanKey<Body, Brand>) && FrameCallable<Body, T, Fresh, Extent>;

// A body of a keyed frame door that reaches no key of the brand of its owner
// and holds a linear value by value: the linear twin of each keyed door binds
// to it.
template <class Body, class T, class Brand, class Fresh, std::size_t Extent>
concept KeyedLinearFrameBody = LinearFrameBody<Body, T, Fresh, Extent> && ReachesNoLoanKey<Body, Brand>;

// A body of a keyed frame door that reaches no key of the brand of its owner
// and stores a borrow outside the frame: the store twin of each keyed door
// binds to it.
template <class Body, class T, class Brand, class Fresh, std::size_t Extent>
concept KeyedStoringFrameBody = StoringFrameBody<Body, T, Fresh, Extent> && ReachesNoLoanKey<Body, Brand>;

// A body of an element door that takes the element as Element& and holds a
// linear value by value: the linear twin of each element door binds to it.
template <class Body, class Element>
concept LinearElementBody = (!::foundation::linear::HoldsNoLinearValue<Body>) && std::is_invocable_v<Body&&, Element&>;

namespace detail {

// The target of a pointer, or of a borrow through a View, a ViewMut or a Ref,
// with no cv, or the type itself for each other type.  The type is bare.
[[nodiscard]] consteval std::meta::info target_of_part_(std::meta::info bare) {
    namespace refl = ::foundation::reflect;
    bool const is_borrow = std::meta::has_template_arguments(bare)
                        && (std::meta::template_of(bare) == ^^View || std::meta::template_of(bare) == ^^ViewMut
                            || std::meta::template_of(bare) == ^^Ref);
    if (!std::meta::is_pointer_type(bare) && !is_borrow) return bare;
    if (is_borrow) return ::foundation::transfer::detail::borrow_target_of(bare).node.type;
    return refl::argument_components_of(refl::TypeNode{bare, false}).begin()[0].type;
}

// True when the type names the element or the array, by value, through a
// reference or a pointer, or as the target of a borrow (target_of_part_).
[[nodiscard]] consteval bool names_array_part_(std::meta::info given, std::meta::info element, std::meta::info array) {
    std::meta::info const target = target_of_part_(std::meta::remove_cvref(std::meta::dealias(given)));
    return target == element || target == array;
}

// True when a direct member of the class names the element or the array
// (names_array_part_), as a member of the owner of the array does.
[[nodiscard]] consteval bool holds_array_part_(std::meta::info type, std::meta::info element, std::meta::info array) {
    namespace refl = ::foundation::reflect;
    if (!std::meta::is_class_type(type) || !std::meta::is_complete_type(type)) return false;
    for (refl::TypeNode const& member :
         refl::member_components_of(refl::TypeNode{type, true}, refl::SpecializationRead::Instantiating)) {
        if (names_array_part_(member.type, element, array)) return true;
    }
    return false;
}

// True when an argument of build_at reaches the element or the array: the
// argument names one of them (names_array_part_), or the argument or its
// target holds one of them in a direct member, as the owner of the array
// does.  The test reads the direct members and no deeper part, so an argument
// of a class of another library, such as a tensor of PyTorch, passes.
// Complexity: linear in the direct members, at compile time only.
[[nodiscard]] consteval bool reaches_array_part_(std::meta::info given, std::meta::info element,
                                                 std::meta::info array) {
    std::meta::info const bare = std::meta::remove_cvref(std::meta::dealias(given));
    std::meta::info const target = target_of_part_(bare);
    return target == element || target == array || holds_array_part_(bare, element, array)
        || holds_array_part_(target, element, array);
}

}  // namespace detail

// An element that the update door copies out of its slot: a copy and a move of
// it do not throw.  So the array holds a whole element while the body runs.
template <class T>
concept CopiedOutElement = std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_move_constructible_v<T>;

// An element that the update door puts back with a live element in the slot at
// each code of the element.  A nothrow assignment writes the live element of
// the slot.  With no such assignment, the element of the slot ends and the copy
// starts there, so the end and the move are trivial and no code of the element
// runs between the two.  The code of an element can reach its array, through a
// static array, a Ref or a guard that takes itself out of a registry.  It would
// find an ended element in the slot.
template <class T>
concept PutBackElement = std::is_nothrow_move_assignable_v<T>
                      || (std::is_trivially_move_constructible_v<T> && std::is_trivially_destructible_v<T>);

// The element of the update door.
template <class T>
concept UpdatableElement = CopiedOutElement<T> && PutBackElement<T>;

// The element of the update door of an owner of heap storage (a HeapArray or
// a MaskedArray).  The door copies the element out of its slot and puts the
// copy back while the owner holds its storage.  Code of the copy or of the
// assignment could reach the owner through a second name and free the storage
// under the slot (the code of an element of foundation/core/Owner.h).  So the
// element is trivially copyable, and the copy and the put-back run no code.
template <class T>
concept HeapUpdatableElement = UpdatableElement<T> && std::is_trivially_copyable_v<T>;

namespace detail {

[[noreturn]] CRUCIBLE_COLD inline void report_lost_update_() noexcept {
    fatal("a body of update wrote the element that the door then puts back, so that write is lost: change the element "
          "only through the argument of the body");
}

// The test oracle of the update door: the bytes of the element at the start
// of the call.  A build without the test oracles (has_test_oracles of
// foundation/Platform.h) has none of it, and neither has an element that is
// not trivially copyable.
template <class T, bool Checks = ::foundation::has_test_oracles && std::is_trivially_copyable_v<T>>
class LostUpdateOracle final {
public:
    explicit constexpr LostUpdateOracle(T const& /*slot*/) noexcept {}
    constexpr void require_unwritten(T const& /*slot*/) const noexcept {}
};

template <class T>
class LostUpdateOracle<T, true> final {
    T entry_;

public:
    // The copy of the bytes holds the padding too, so the compare reads no
    // byte that the copy did not write.
    explicit constexpr LostUpdateOracle(T const& slot) noexcept : entry_(slot) {
        if !consteval {
            __builtin_memcpy(static_cast<void*>(__builtin_addressof(entry_)), __builtin_addressof(slot), sizeof(T));
        }
    }
    constexpr void require_unwritten(T const& slot) const noexcept {
        if !consteval {
            if (__builtin_memcmp(__builtin_addressof(entry_), __builtin_addressof(slot), sizeof(T)) != 0) [[unlikely]] {
                report_lost_update_();
            }
        }
    }
};

// The frame of one call of update: the copy of the element, which the
// destructor puts back in the place of the element.  The result of the body
// exists before the destructor runs.
template <class T>
    requires UpdatableElement<T>
class UpdateFrame final {
    T& slot_;
    T held_;
    [[no_unique_address]] LostUpdateOracle<T> oracle_;

public:
    explicit constexpr UpdateFrame(T& slot) noexcept : slot_{slot}, held_(slot), oracle_{slot} {}
    UpdateFrame(UpdateFrame const&) = delete("the frame of update puts its copy back one time");
    UpdateFrame& operator=(UpdateFrame const&) = delete("the frame of update puts its copy back one time");
    // An element with no assignment ends, and the copy starts in its place.
    // Its end and its move are trivial (PutBackElement), so no code of the
    // element runs while the slot holds no element.
    constexpr ~UpdateFrame() {
        oracle_.require_unwritten(slot_);
        if constexpr (std::is_nothrow_move_assignable_v<T>) {
            slot_ = static_cast<T&&>(held_);
        } else {
            slot_.~T();
            ::new(static_cast<void*>(__builtin_launder(__builtin_addressof(slot_)))) T(static_cast<T&&>(held_));
        }
    }
    // The call of the body with the copy.  It gives the result of the body.
    template <class Body>
    constexpr auto run(Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, T&>) {
        return static_cast<Body&&>(body)(held_);
    }
};

}  // namespace detail

// The function of the value door build_at of a FixedArray: it holds no
// state, so the door has no captures to read.  It is a function, a pointer
// to a function, or a class with no member, such as a lambda with no
// capture.  Each value that it reads comes as an argument of the door.
template <class Fn>
concept StatelessMake = std::is_function_v<std::remove_cvref_t<Fn>>
                     || (std::is_pointer_v<std::remove_cvref_t<Fn>>
                         && std::is_function_v<std::remove_pointer_t<std::remove_cvref_t<Fn>>>)
                     || (std::is_class_v<std::remove_cvref_t<Fn>> && std::is_empty_v<std::remove_cvref_t<Fn>>);

// The function and the arguments of build_at: the function gives exactly
// the element with no throw for the arguments, and the element has a trivial
// destructor, so the door ends the old element with no call.
template <class Fn, class Element, class... Args>
concept MakesElementInPlace =
    std::is_nothrow_invocable_v<Fn&, Args&&...> && std::same_as<std::invoke_result_t<Fn&, Args&&...>, Element>
    && std::is_trivially_destructible_v<Element>;

// The element of build_at that the function builds beside its slot: its move runs no
// code, and it fits in the two registers of a result.  So the move into the slot
// after the call costs no step more than a build in the slot.
template <class T>
concept BuiltBesideItsSlot = std::is_trivially_move_constructible_v<T> && sizeof(T) <= 16;

// No argument of build_at reaches the element or the array
// (reaches_array_part_), so the function reads no element while the new
// element takes its storage.
template <class Element, class Array, class... Args>
concept ArgumentsReachNoArrayPart = (!detail::reaches_array_part_(^^Args, ^^Element, ^^Array) && ...);

// No argument of build_at that comes by value holds a linear value.  An
// argument by an lvalue reference stays in the frame of the caller.
template <class... Args>
concept ArgumentsHoldNoLinearValue =
    ((std::is_lvalue_reference_v<Args> || ::foundation::linear::HoldsNoLinearValue<Args>) && ...);

namespace detail {

// Copies the elements of source into run from the position start, as the
// value door copy_window of an array does.  A source that does not fit gives
// LengthMismatch, and the function then writes nothing.  A View has no brand,
// so the source can be a View of run, and the copy is one memmove.
// Complexity: O(source.size()).
template <class T, std::size_t N>
    requires RegionCopyElement<T> && (N != dynamic_extent)
[[nodiscard]] CRUCIBLE_INLINE Result<Unit, LengthMismatch> copy_into_window_(View<T, N> run, std::size_t start,
                                                                             View<T const> source) noexcept {
    if (start > N || source.size() > N - start) return err(LengthMismatch{});
    __builtin_memmove(first_of_(BaseLayerRead{}, run) + start, first_of_(BaseLayerRead{}, source),
                      source.size() * sizeof(T));
    return Unit{};
}

}  // namespace detail

// The body of a keyed element door: it takes the element with no throw, its
// result holds no borrow and fits a Record, it reaches no key of the brand,
// and it holds no linear value by value.
template <class Body, class T, class Brand>
concept KeyedElementBody = std::is_nothrow_invocable_v<Body&&, T&>
                        && (std::is_void_v<detail::body_result_t<Body&&, T&>>
                            || (HoldsNoBorrow<detail::body_result_t<Body&&, T&>>
                                && RecordElement<std::remove_cv_t<detail::body_result_t<Body&&, T&>>>))
                        && ReachesNoLoanKey<Body, Brand> && ReachesNoLoanKey<T, Brand>
                        && ::foundation::linear::HoldsNoLinearValue<Body> && StoresNoBorrowOutside<Body>;

// A body of a keyed element door that reaches no key of the brand and holds
// a linear value by value: the linear twin of each keyed element door binds
// to it.
template <class Body, class T, class Brand>
concept KeyedLinearElementBody = LinearElementBody<Body, T> && ReachesNoLoanKey<Body, Brand>;

template <class T, std::size_t N, ::foundation::brand::IsBrand KeyBrand = Unkeyed>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N>
class[[= loan_key_mark_when<IsKeyBrand<KeyBrand>>]][[= equal_parts{}]] FixedArray final
    : private detail::array_storage_t<T, N, KeyBrand> {
    using Storage = detail::array_storage_t<T, N, KeyBrand>;
    using Storage::elements_;

    // An array of a key brand opens its frames only with its LoanKey (the
    // loan key of foundation/core/Frame.h).  An array of Unkeyed, the default,
    // has the value doors and opens no frame.
    static constexpr bool is_keyed_ = IsKeyBrand<KeyBrand>;

public:
    using element_type = T;

    // Each element value-initialized: an integer is zero.
    constexpr FixedArray() noexcept
        requires std::is_nothrow_default_constructible_v<T>
    = default;

    // The constructor of N elements.
    using Storage::Storage;

    [[nodiscard]] static constexpr std::size_t size() noexcept { return N; }

    // The View of the const elements.  It borrows this array.  The array
    // holds its N elements in a C array, so the View comes through the typed
    // door of a C array, with no check and no test of a null pointer.  A write
    // takes a value door, or a door with the key of the array, so no two Views
    // of one array write the same elements.  Each call names N: GCC 16 deduces
    // the bound of an array of 2^34 - 1 bytes as the largest size_t.
    [[nodiscard]] constexpr View<T const, N> view() const& noexcept {
        return detail::view_of_array_<T const, N>(elements_);
    }
    void view() && = delete("a view of a temporary FixedArray points into an object that the full expression ends");
    void
    view() const&& = delete("a view of a temporary FixedArray points into an object that the full expression ends");

    // The run of the elements for the frame door with_mut: a View that writes
    // them.  The key of a frame keeps each caller outside the base out, so
    // only a frame door gives a View that writes the elements of an array.
    [[nodiscard]] friend constexpr View<T, N> frame_run_(detail::FrameKey const& /*key*/, FixedArray& array) noexcept {
        return detail::view_of_array_<T, N>(array.elements_);
    }

    // The loop `for (T const& element : array)`.  A loop over a temporary
    // binds it to a reference first, and a cursor of an rvalue is refused.
    [[nodiscard]] constexpr ViewCursor<T const> begin() const& noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<T const> end() const& noexcept { return view().end(); }
    void begin() && = delete("a cursor of a temporary FixedArray points into an object that the full expression ends");
    void end() && = delete("a cursor of a temporary FixedArray points into an object that the full expression ends");
    void
    begin() const&& = delete("a cursor of a temporary FixedArray points into an object that the full expression ends");
    void
    end() const&& = delete("a cursor of a temporary FixedArray points into an object that the full expression ends");

    // The element at a proven index, for a read.  index<I>() converts to an
    // Index<N> when I < N, so a constant index needs no check either.
    [[nodiscard]] constexpr T const& at(Index<N> index) const& noexcept { return elements_[index.raw()]; }
    T& at(Index<N>) && = delete("an element of a temporary FixedArray dangles at the end of the full expression");
    T const&
        at(Index<N>) const&& = delete("an element of a temporary FixedArray dangles at the end of the full expression");

    // The frame door of one element (the element door): body gets the
    // element at a proven index in place, for the time of the call, and the
    // door gives the result of body.  The result holds no borrow, so no
    // reference to the element leaves the door.  The door gives that result
    // by value: a deduced auto type is never a reference, and it reads the
    // call of body with no trait.  A const array gives a const element.  An
    // array with no key gives an element that body writes only when two
    // threads can write it at the same time, such as an Atomic: a second
    // reference to it, through another name of the array, is then correct.
    // Complexity: O(1), and the cost of body.
    template <class Body>
        requires(!is_keyed_) && ::foundation::transfer::Synchronized<T> && std::is_invocable_v<Body, T&>
             && HoldsNoBorrow<detail::body_result_t<Body, T&>> && ::foundation::linear::HoldsNoLinearValue<Body>
             && StoresNoBorrowOutside<Body>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto
        with_element(Index<N> index, Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, T&>) {
        return static_cast<Body&&>(body)(elements_[index.raw()]);
    }
    template <class Body>
        requires(!is_keyed_) && ::foundation::transfer::Synchronized<T> && LinearElementBody<Body, T>
    void with_element(Index<N> index, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires(!is_keyed_) && (!::foundation::transfer::Synchronized<T>)
                 && std::is_invocable_v<Body, T&> && (!std::is_invocable_v<Body, T const&>)
    void with_element(Index<N> index, Body&& body) & = delete(
        "a FixedArray with no key lends no element that a body writes, because another name of the array could lend "
        "the same element: call set, exchange or update(index, body), or make the array with mint_keyed_owner and "
        "call with_element(key, index, body)");
    template <class Body>
        requires is_keyed_ && std::is_invocable_v<Body, T&>
    void with_element(Index<N> index, Body&& body) & = delete(
        "a keyed FixedArray lends an element that a body writes only with its LoanKey: call with_element(key, index, "
        "body)");
    template <class Body>
        requires std::is_invocable_v<Body, T const&> && HoldsNoBorrow<detail::body_result_t<Body, T const&>>
              && ::foundation::linear::HoldsNoLinearValue<Body> && StoresNoBorrowOutside<Body>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto
        with_element(Index<N> index, Body&& body) const& noexcept(std::is_nothrow_invocable_v<Body, T const&>) {
        return static_cast<Body&&>(body)(elements_[index.raw()]);
    }
    template <class Body>
        requires LinearElementBody<Body, T const>
    void with_element(Index<N> index, Body&& body) const& = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
    void with_element(Index<N> index, Body&& body) && =
        delete("a frame door of a temporary FixedArray lends an element that the full expression ends");
    template <class Body>
    void with_element(Index<N> index, Body&& body) const&& =
        delete("a frame door of a temporary FixedArray lends an element that the full expression ends");

    // The value doors of one element (the value doors): the element at a
    // proven index becomes value.  Each door takes the value as an argument,
    // so no code of the caller runs while it writes.  set ends the old value,
    // so the element is one that a holder can drop.  exchange gives the old
    // value back.  Complexity: O(1), and the cost of the assignment.
    constexpr void set(Index<N> index, T const& value) & noexcept
        requires(!is_keyed_) && std::is_nothrow_copy_assignable_v<T> && DroppablePayload<T>
    {
        elements_[index.raw()] = value;
    }
    constexpr void set(Index<N> index, T&& value) & noexcept
        requires(!is_keyed_) && std::is_nothrow_move_assignable_v<T> && DroppablePayload<T>
    {
        elements_[index.raw()] = static_cast<T&&>(value);
    }
    [[nodiscard]] constexpr T exchange(Index<N> index, T value) & noexcept
        requires(!is_keyed_) && std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>
    {
        T held = static_cast<T&&>(elements_[index.raw()]);
        elements_[index.raw()] = static_cast<T&&>(value);
        return held;
    }
    // The update door of one element: body gets a copy of the element at a
    // proven index, and then the door puts the copy in the place of the
    // element.  The door gives the result of body, which holds no borrow.  No
    // reference of the door names the storage of the array while body runs, so
    // a second door of the array, through a helper or an alias, finds whole
    // elements, and the door takes no key.  A nested update or a nested set of
    // the same element loses its write when the door puts the copy back.  That
    // is a wrong value and no memory error, and no type can refuse it.  So a
    // build with the test oracles ends the process when the bytes of a
    // trivially copyable element change while body runs.  An element with no
    // assignment, such as one with a Monotonic part, ends, and the copy starts
    // in its place, so such a part moves only by its own steps in body.  The
    // end and the move of such an element are trivial (PutBackElement).
    // Complexity: O(1), and the cost of body, of one copy and of one move.
    template <class Body>
        requires(!is_keyed_)
             && UpdatableElement<T> && std::is_invocable_v<Body, T&> && HoldsNoBorrow<detail::body_result_t<Body, T&>>
             && ::foundation::linear::HoldsNoLinearValue<Body> && StoresNoBorrowOutside<Body>
    [[= ::foundation::linear::borrow_match{}]] constexpr auto
        update(Index<N> index, Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, T&>) {
        detail::UpdateFrame<T> frame{elements_[index.raw()]};
        return frame.run(static_cast<Body&&>(body));
    }
    template <class Body>
        requires(!is_keyed_) && LinearElementBody<Body, T>
    void update(Index<N> index, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires(!is_keyed_) && (!CopiedOutElement<T>)
                 && std::is_invocable_v<Body, T&> && ::foundation::linear::HoldsNoLinearValue<Body>
    void update(Index<N> index, Body&& body) & = delete(
        "update gives the body a copy of the element and moves the copy back, so the element copies and moves with no "
        "throw: take an element that does not copy with exchange");
    template <class Body>
        requires(!is_keyed_) && CopiedOutElement<T> && (!PutBackElement<T>)
                 && std::is_invocable_v<Body, T&> && ::foundation::linear::HoldsNoLinearValue<Body>
    void update(Index<N> index, Body&& body) & = delete(
        "update of an element with no nothrow assignment ends the element in its slot and starts the copy there, so "
        "the move and the end of the element run no code: give the element a nothrow assignment, or make the array "
        "with mint_keyed_owner and call with_element(key, index, body)");
    template <class... Arguments>
        requires is_keyed_
    void update(Arguments&&... arguments) & = delete(
        "a keyed FixedArray has no update door, because another name of it could write an element while a frame of it "
        "is open: call with_element(key, index, body)");
    template <class... Arguments>
        requires is_keyed_
    void set(Arguments&&... arguments) & = delete(
        "a keyed FixedArray has no value door, because another name of it could write an element while a frame of it "
        "is open: call with_element(key, index, body)");
    template <class... Arguments>
        requires is_keyed_
    void exchange(Arguments&&... arguments) & = delete(
        "a keyed FixedArray has no value door, because another name of it could write an element while a frame of it "
        "is open: call with_element(key, index, body)");

    // The value doors of the run (the value doors): fill, copy_from of a run
    // of N elements, and copy_window of a run that fits from start.  The
    // two copies give the array the values that the source held before the
    // copy, also when the source is a View of the same array.  A constant
    // evaluation copies each element, because it takes no copy of bytes.  The
    // source of a run of N elements of this array is this array, so the order
    // of the elements does not change the result.  Complexity: O(N), and
    // O(source.size()) for a window.
    constexpr void fill(T const& value) & noexcept
        requires(!is_keyed_) && RegionCopyElement<T>
    {
        ::foundation::core::fill(detail::view_of_array_<T, N>(elements_), value);
    }
    CRUCIBLE_INLINE constexpr void copy_from(View<T const, N> source) & noexcept
        requires(!is_keyed_) && RegionCopyElement<T>
    {
        if consteval {
            for (Index<N> const index : indices())
                elements_[index.raw()] = source.at(index);
        } else {
            ::foundation::core::copy(detail::view_of_array_<T, N>(elements_), source);
        }
    }
    [[nodiscard]] CRUCIBLE_INLINE Result<Unit, LengthMismatch> copy_window(std::size_t start,
                                                                           View<T const> source) & noexcept
        requires(!is_keyed_) && RegionCopyElement<T>
    {
        return detail::copy_into_window_(detail::view_of_array_<T, N>(elements_), start, source);
    }
    template <class... Arguments>
        requires is_keyed_
    void fill(Arguments&&... arguments) & = delete(
        "a keyed FixedArray has no value door, because another name of it could write the run while a frame of it is "
        "open: call with_mut(key, body)");
    template <class... Arguments>
        requires is_keyed_
    void copy_from(Arguments&&... arguments) & = delete(
        "a keyed FixedArray has no value door, because another name of it could write the run while a frame of it is "
        "open: call with_mut(key, body)");
    template <class... Arguments>
        requires is_keyed_
    void copy_window(Arguments&&... arguments) & = delete(
        "a keyed FixedArray has no value door, because another name of it could write the run while a frame of it is "
        "open: call with_mut(key, body)");

    // The value door of one element: the element at a proven index becomes
    // the value that fn(args...) gives, built in its place, and the door
    // gives nothing.  The value builds the new element in the storage of the
    // old one, which the trivial destructor of T ends with no call, so no
    // byte of the element copies or moves.  The new element takes its
    // storage while fn runs, so fn must reach no element of the array, and
    // the shape of the door holds that: fn holds no state (StatelessMake), so
    // each value that it reads is an argument, and no argument names the
    // array, an element of it, or a borrow of one of them, or holds one of
    // them in a direct member, as the owner of the array does
    // (ArgumentsReachNoArrayPart).  The door lends no borrow of the array, so
    // it has no store check, and an argument of a class of another library,
    // such as a tensor of PyTorch, passes.  The one route to the array that
    // the door does not see is an object of static storage, or an argument
    // that reaches the array through more than one step.  T has a trivial
    // destructor.  An argument by value holds no linear value.
    //
    // An element of at most 16 bytes with a trivial move (BuiltBesideItsSlot) goes
    // into the slot after fn returned, so a fn that reads the array by that route
    // reads the old element.  A larger element, and one whose move is not trivial,
    // can take the slot while fn runs.  A fn that reads the same element of a
    // static array then reads a value with old parts and new parts.  The read stays
    // in the storage of the array, and no element ends there, so the harm is a value
    // and not memory.  Complexity: O(1), and the cost of fn.
    template <class Fn, class... Args>
        requires(!is_keyed_)
             && MakesElementInPlace<Fn, T, Args...> && StatelessMake<Fn> && ::foundation::linear::HoldsNoLinearValue<Fn>
             && ArgumentsReachNoArrayPart<T, FixedArray, Args...> && ArgumentsHoldNoLinearValue<Args...>
    void build_at(Index<N> index, Fn&& fn, Args&&... args) & noexcept {
        // GCC reads an array of one element at the end of a class as an array
        // of one byte, and its check of a placement new then refuses the
        // build.  The address through __builtin_launder is the same address,
        // and the check reads no array there.
        void* const slot = __builtin_launder(&elements_[index.raw()]);
        if constexpr (BuiltBesideItsSlot<T>) {
            T made = fn(static_cast<Args&&>(args)...);
            ::new(slot) T(static_cast<T&&>(made));
        } else {
            ::new(slot) T(fn(static_cast<Args&&>(args)...));
        }
    }
    template <class Fn, class... Args>
        requires(!is_keyed_) && MakesElementInPlace<Fn, T, Args...> && (!StatelessMake<Fn>)
    void build_at(Index<N> index, Fn&& fn, Args&&... args) & = delete(
        "the function of build_at holds no state, so the door sees each value that it reads: give a function or a "
        "lambda with no capture, and give each value that it reads as an argument of build_at");
    template <class Fn, class... Args>
        requires(!is_keyed_) && MakesElementInPlace<Fn, T, Args...> && StatelessMake<Fn>
                 && (!ArgumentsReachNoArrayPart<T, FixedArray, Args...>)
    void build_at(Index<N> index, Fn&& fn, Args&&... args) & = delete(
        "an argument of build_at names the array, an element of it or a borrow of one of them, or holds one of them: "
        "the new element takes its storage while the function runs, so the function reads no element of the array");
    template <class Fn, class... Args>
        requires(!is_keyed_) && MakesElementInPlace<Fn, T, Args...> && StatelessMake<Fn>
                 && ArgumentsReachNoArrayPart<T, FixedArray, Args...> && (!ArgumentsHoldNoLinearValue<Args...>)
    void build_at(Index<N> index, Fn&& fn, Args&&... args) & = delete(
        "an argument of build_at holds a linear value, and the door gives it to one call that need not consume it: "
        "keep the linear value a local of the caller, and give the function a reference to it");
    template <class Fn, class... Args>
        requires is_keyed_
    void build_at(Index<N> index, Fn&& fn, Args&&... args) & = delete(
        "a keyed FixedArray writes an element only through the frame doors that take its LoanKey: call with_element("
        "key, index, body)");
    template <class Fn, class... Args>
        requires MakesElementInPlace<Fn, T, Args...>
    void build_at(Index<N> index, Fn&& fn,
                  Args&&... args) const& = delete("a const FixedArray has no writer of its elements");
    template <class Fn, class... Args>
        requires MakesElementInPlace<Fn, T, Args...>
    void build_at(Index<N> index, Fn&& fn, Args&&... args) && =
        delete("a temporary FixedArray ends at the end of the full expression, so a write of it has no reader");

    // An array with no key opens no frame, and a keyed array opens a frame
    // only with its key, so each body with no key binds to one of these two
    // overloads, and the refusal gives its reason.  The two read no call of
    // the body, so a body with an error of its own binds to them too.
    template <class Body>
        requires(!is_keyed_)
    void with_mut(Body&& body) & = delete(
        "a FixedArray with no key opens no frame, because another name of the array could open a second frame of the "
        "same run: call fill, copy_from, copy_window or build_fixed_array, or make the array with mint_keyed_owner and "
        "call with_mut(key, body)");
    template <class Body>
        requires is_keyed_
    void with_mut(Body&& body) & =
        delete("a keyed FixedArray opens a frame only with its LoanKey: call with_mut(std::move(key), body) or "
               "with_mut(key, body)");

    // Keyed frame door (the loan key of foundation/core/Frame.h): body
    // gets the ViewMut<T, Fresh, N> of the elements in place, with the scope
    // brand of the frame.  The array stays in place, and the door writes no
    // byte of it, so it copies no element.  The result of body leaves the
    // frame (FrameResult of Choice.h), by value.  The key moves into the door,
    // and the door gives it back: alone for a body that gives void, and after
    // the result in a Record for each other body.  The body reaches no key of
    // the brand of the array (the gate), so no code inside the frame opens a
    // second frame of the array.  The frame brand is the scope brand of the
    // call, so copy() takes its memcpy and write_each its restrict loop.  The
    // pack refuses each explicit argument, so no caller names the brand.
    // Complexity: O(1), and the cost of body.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires NamesNoArgument<Refused...> && KeyedFrameBody<Body, T, KeyBrand, Fresh, N>
    [[nodiscard]] constexpr detail::frame_return_t<detail::body_result_t<Body&&, ViewMut<T, Fresh, N>>,
                                                   LoanKey<KeyBrand>>
    with_mut(LoanKey<KeyBrand>&& key, Body&& body) & noexcept {
        return detail::give_run_back_<Fresh>(ArrayFrameKeyDoor::key_(), std::move(key),
                                             detail::view_of_array_<T, N>(elements_), static_cast<Body&&>(body));
    }

    // The same door with a borrow of the key for the call: body gets the
    // ViewMut, the key stays with the caller, and the door gives the result
    // of body.  A root door lends the key of a named brand in this form.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires NamesNoArgument<Refused...> && KeyedFrameBody<Body, T, KeyBrand, Fresh, N>
    constexpr auto with_mut(LoanKey<KeyBrand>& /*key*/, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(
            ViewMut<T, Fresh, N>{ArrayFrameKeyDoor::key_(), detail::view_of_array_<T, N>(elements_)});
    }

    // A body that reaches a key of the brand of the array binds to these
    // overloads, so the refusal gives its reason.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyReachingFrameBody<Body, T, KeyBrand, Fresh, N>
    void with_mut(LoanKey<KeyBrand>&& key, Body&& body) & = delete(
        "a body of a keyed frame reaches a LoanKey of the brand of its owner, so code in the body could open a second "
        "frame of the owner: capture only what the body reads, and keep the key with the caller");
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyReachingFrameBody<Body, T, KeyBrand, Fresh, N>
    void with_mut(LoanKey<KeyBrand>& key, Body&& body) & = delete(
        "a body of a keyed frame reaches a LoanKey of the brand of its owner, so code in the body could open a second "
        "frame of the owner: capture only what the body reads, and keep the key with the caller");
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyedLinearFrameBody<Body, T, KeyBrand, Fresh, N>
    void with_mut(LoanKey<KeyBrand>&& key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyedLinearFrameBody<Body, T, KeyBrand, Fresh, N>
    void with_mut(LoanKey<KeyBrand>& key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    // A body whose result holds a borrow or names the brand of the frame, and
    // a body that stores a borrow outside the frame, bind to these overloads,
    // so each refusal gives its own reason.  The first two are the twins of
    // KeyedFrameBody, so they bind also to a body that does not take the
    // ViewMut or that can throw.
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires NamesNoArgument<Refused...> && KeyedFrameGates<Body, T, KeyBrand>
    void with_mut(LoanKey<KeyBrand>&& key, Body&& body) & = delete(CRUCIBLE_FRAME_BODY_REFUSAL);
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires NamesNoArgument<Refused...> && KeyedFrameGates<Body, T, KeyBrand>
    void with_mut(LoanKey<KeyBrand>& key, Body&& body) & = delete(CRUCIBLE_FRAME_BODY_REFUSAL);
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyedStoringFrameBody<Body, T, KeyBrand, Fresh, N>
    void with_mut(LoanKey<KeyBrand>&& key, Body&& body) & = delete(CRUCIBLE_STORING_BODY_REFUSAL);
    template <class... Refused, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
        requires(sizeof...(Refused) == 0) && is_keyed_ && KeyedStoringFrameBody<Body, T, KeyBrand, Fresh, N>
    void with_mut(LoanKey<KeyBrand>& key, Body&& body) & = delete(CRUCIBLE_STORING_BODY_REFUSAL);

    // Keyed element door: body gets the element at a proven index in
    // place, as T&, and the key comes back as with the keyed frame door: alone
    // for a body that gives void, and after the result in a Record for each
    // other body.  The borrow form keeps the key with the caller and gives
    // the result of body.  The body reaches no key of the brand (the gate), so
    // no code inside the call gives a second reference to the element.
    // Complexity: O(1), and the cost of body.
    template <class Body>
        requires is_keyed_ && KeyedElementBody<Body, T, KeyBrand>
    [[nodiscard]] constexpr detail::frame_return_t<detail::body_result_t<Body&&, T&>, LoanKey<KeyBrand>>
    with_element(LoanKey<KeyBrand>&& key, Index<N> index, Body&& body) & noexcept {
        using Given = detail::body_result_t<Body&&, T&>;
        if constexpr (std::is_void_v<Given>) {
            static_cast<Body&&>(body)(elements_[index.raw()]);
            return std::move(key);
        } else {
            return detail::frame_return_t<Given, LoanKey<KeyBrand>>{static_cast<Body&&>(body)(elements_[index.raw()]),
                                                                    std::move(key)};
        }
    }
    template <class Body>
        requires is_keyed_ && KeyedElementBody<Body, T, KeyBrand>
    constexpr auto with_element(LoanKey<KeyBrand>& /*key*/, Index<N> index, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(elements_[index.raw()]);
    }
    template <class Body>
        requires is_keyed_ && std::is_nothrow_invocable_v<Body&&, T&> && (!ReachesNoLoanKey<Body, KeyBrand>)
    void with_element(LoanKey<KeyBrand>&& key, Index<N> index, Body&& body) & = delete(
        "a body of a keyed element door reaches a LoanKey of the brand of its owner, so code in the body could give a "
        "second reference to the element: capture only what the body reads, and keep the key with the caller");
    template <class Body>
        requires is_keyed_ && std::is_nothrow_invocable_v<Body&&, T&> && (!ReachesNoLoanKey<Body, KeyBrand>)
    void with_element(LoanKey<KeyBrand>& key, Index<N> index, Body&& body) & = delete(
        "a body of a keyed element door reaches a LoanKey of the brand of its owner, so code in the body could give a "
        "second reference to the element: capture only what the body reads, and keep the key with the caller");
    template <class Body>
        requires is_keyed_ && KeyedLinearElementBody<Body, T, KeyBrand>
    void with_element(LoanKey<KeyBrand>&& key, Index<N> index, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires is_keyed_ && KeyedLinearElementBody<Body, T, KeyBrand>
    void with_element(LoanKey<KeyBrand>& key, Index<N> index, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    // A const array lends no writer, so each body binds to this overload,
    // and the refusal gives its reason.
    template <class Body>
    void with_mut(Body&& body) const& = delete("a const FixedArray lends no writer of its elements: read them through "
                                               "view(), or take a FixedArray that is not const");
    template <class Body>
    void with_mut(Body&& body) && =
        delete("a frame door of a temporary FixedArray lends elements that the full expression ends");
    template <class Body>
    void with_mut(Body&& body) const&& =
        delete("a frame door of a temporary FixedArray lends elements that the full expression ends");

    // The element at a position that no type proves, as an Option of a
    // Ref with the fresh brand of the call, or no value at or past N: one
    // compare.  The pack refuses each explicit argument, and the result
    // type holds the brand in undeducible_brand_t.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) const& noexcept {
        return ArrayRefDoor::element_at_<Fresh>(&elements_[0], N, position);
    }
    void get(std::size_t) && = delete("an element of a temporary FixedArray dangles at the end of the full expression");
    void
    get(std::size_t) const&& = delete("an element of a temporary FixedArray dangles at the end of the full expression");

    // Each index of the array, in order: the loop
    // `for (Index<N> index : array.indices())`.
    [[nodiscard]] static constexpr IndexRange<N> indices() noexcept { return IndexRange<N>{}; }

    // The iteration verbs of the View of the elements: each element with
    // its index, each element from the last, each run of Width consecutive
    // elements, and each run of Width elements with the rest.  The loop of a
    // temporary array is refused, because its elements would dangle.
    // Each Ref of one call of enumerate() has the fresh brand of that call,
    // and the pack refuses each explicit argument.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr auto enumerate() const& noexcept {
        using Stage = detail::EnumerateStage<T const, N, Fresh, N>;
        return Walk<Stage>{ArrayRefDoor::enumerate_stage_<Fresh>(view())};
    }
    void enumerate() const&& =
        delete("a loop over a temporary FixedArray points into an object that the full expression ends");

    [[nodiscard]] constexpr auto reverse() const& noexcept { return view().reverse(); }
    void reverse() const&& =
        delete("a loop over a temporary FixedArray points into an object that the full expression ends");

    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width <= N)
    [[nodiscard]] constexpr auto windows() const& noexcept {
        return view().template windows<Width>();
    }
    template <std::size_t Width>
    void windows() const&& =
        delete("a loop over a temporary FixedArray points into an object that the full expression ends");

    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width <= N)
    [[nodiscard]] constexpr auto chunks() const& noexcept {
        return view().template chunks<Width>();
    }
    template <std::size_t Width>
    void
    chunks() const&& = delete("a loop over a temporary FixedArray points into an object that the full expression ends");

    // Two arrays are equal when each element equals the element at the same
    // position of the other array, and they order as the first pair of
    // elements that differ.  equal and order of the Protocol family compare
    // the elements, so a float element compares by its bits.
    [[nodiscard]] friend constexpr bool operator==(FixedArray const& left, FixedArray const& right) noexcept
        requires Equatable<T>
    {
        return ::foundation::core::equal(left.view(), right.view());
    }
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(FixedArray const& left,
                                                                    FixedArray const& right) noexcept
        requires Orderable<T>
    {
        // Two arrays of one type hold one count, so the order is the order of
        // the C arrays, with no compare of the counts.
        return ::foundation::core::order(left.elements_, right.elements_);
    }
};

// The build door (the build door): a new array of N value-initialized
// elements.  body gets the ViewMut<T, Fresh, N> of the elements, with the
// scope brand of the call, and the door gives the array: alone for a body
// that gives void, and after the result in a Record for each other body.  The
// array is a local of the door until the door gives it, so no name of it is
// in the scope of the caller, and its frame needs no LoanKey.  The compiler
// builds a result of the array alone in the object that the call
// initializes.  So the gate BuildsUnnamedArray refuses a body that could write
// that object.  The other gates are those of each frame body.  The pack
// refuses each explicit argument after N, so no caller names the brand.
// Complexity: O(N) for the value-initialization, and the cost of body.
template <class T, std::size_t N, class... Refused, class Body, ::foundation::brand::IsBrand Fresh>
    requires NamesNoArgument<Refused...> && BuildsFixedArray<Body, T, N, Fresh>
[[nodiscard]] constexpr detail::frame_return_t<detail::body_result_t<Body&&, ViewMut<T, Fresh, N>>,
                                               FixedArray<T, N, Unkeyed>>
build_fixed_array(Body&& body) noexcept {
    FixedArray<T, N, Unkeyed> held{};
    View<T, N> const run = frame_run_(ArrayFrameKeyDoor::key_(), held);
    if constexpr (std::is_void_v<detail::body_result_t<Body&&, ViewMut<T, Fresh, N>>>) {
        static_cast<Body&&>(body)(ViewMut<T, Fresh, N>{ArrayFrameKeyDoor::key_(), run});
        return held;
    } else {
        return detail::give_run_back_<Fresh>(ArrayFrameKeyDoor::key_(), static_cast<FixedArray<T, N, Unkeyed>&&>(held),
                                             run, static_cast<Body&&>(body));
    }
}

// A body that is no frame callable (the refused twin, FrameBody of
// foundation/core/Region.h), a body that holds a linear value, a body that
// stores a borrow outside the frame, and a body that could write the object
// that the array initializes bind to these overloads, so each refusal gives its
// reason.
template <class T, std::size_t N, class... Refused, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires NamesNoArgument<Refused...> && BuildGates<Body, T, N>
void build_fixed_array(Body&& body) = delete(CRUCIBLE_FRAME_BODY_REFUSAL);
template <class T, std::size_t N, class... Refused, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && BuildableElement<T, N> && LinearFrameBody<Body, T, Fresh, N>
void build_fixed_array(Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
template <class T, std::size_t N, class... Refused, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && BuildableElement<T, N> && StoringFrameBody<Body, T, Fresh, N>
void build_fixed_array(Body&& body) = delete(CRUCIBLE_STORING_BODY_REFUSAL);
template <class T, std::size_t N, class... Refused, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && BuildableElement<T, N> && FrameGates<Body> && (!BuildsUnnamedArray<Body, T, N>)
void build_fixed_array(Body&& body) = delete(
    "a body of build_fixed_array reaches an object that holds an array of the type that it builds, through a "
    "reference or a pointer that is not const, so it could write the object that the array initializes: capture the "
    "object by a const reference, or capture only the part that the body reads");

namespace detail {

// The maker of a keyed FixedArray from a plain array of the same elements,
// which FixedArrayOwner holds as its base.
template <class T, std::size_t N, class Positions = std::make_index_sequence<(N <= max_listed_array_count) ? N : 0>>
struct CopiedArrayMaker_;
template <class T, std::size_t N, std::size_t... Positions>
struct CopiedArrayMaker_<T, N, std::index_sequence<Positions...>> {
    template <IsKeyBrand Brand>
        requires(sizeof...(Positions) == N) && std::is_nothrow_copy_constructible_v<T>
    [[nodiscard]] constexpr FixedArray<T, N, Brand> operator()(MintedBrand<Brand> /*proof*/,
                                                               FixedArray<T, N, Unkeyed> const& values) const noexcept {
        return FixedArray<T, N, Brand>(values.at(index<Positions>())...);
    }
};

}  // namespace detail

// The maker of a keyed FixedArray, for the mint of foundation/core/KeyedMint.h
// (the loan key of foundation/core/Frame.h).  Each element is
// value-initialized, or it is a copy of the element of a plain array of at
// most max_listed_array_count elements:
//   auto [array, key] = mint_keyed_owner(fixed_array_owner<T, N>);
//   auto [array, key] = mint_keyed_owner(fixed_array_owner<int, 3>, FixedArray<int, 3>{1, 2, 3});
// The maker takes the proof of the brand of the mint, so it builds an array
// only inside a mint.  The array comes back as a prvalue, so no element
// moves.  Complexity: O(N) for the initialization.
template <class T, std::size_t N>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N>
struct FixedArrayOwner final : detail::CopiedArrayMaker_<T, N> {
    using detail::CopiedArrayMaker_<T, N>::operator();

    template <IsKeyBrand Brand>
        requires std::is_nothrow_default_constructible_v<T>
    [[nodiscard]] constexpr FixedArray<T, N, Brand> operator()(MintedBrand<Brand> /*proof*/) const noexcept {
        return FixedArray<T, N, Brand>{};
    }
};

template <class T, std::size_t N>
    requires OwnedElement<T> && IndexCount<N> && ViewExtent<T, N>
inline constexpr FixedArrayOwner<T, N> fixed_array_owner{};

// ── HeapArray ──────────────────────────────────────────────────────────────

namespace detail {

// The fatal exit of a count of elements that is longer than the largest View.
[[noreturn]] CRUCIBLE_COLD inline void refuse_heap_array_count_(std::size_t count, std::size_t element_bytes) noexcept {
    fatal("a HeapArray of {} elements of {} bytes is longer than the largest View", count, element_bytes);
}

// Ends the process when count elements of T are longer than the largest View.
// The check always inlines, so the code after it knows the bound: the loop
// that fills the storage then writes at most the bytes of the largest View.
// Complexity: O(1).
template <class T>
CRUCIBLE_INLINE constexpr void require_heap_array_count_(std::size_t count) noexcept {
    if (count > max_view_count(sizeof(T))) [[unlikely]] {
        refuse_heap_array_count_(count, sizeof(T));
    }
}

// The layout of the storage of count elements of T: their bytes rounded up
// to the alignment, at the alignment.  count is above zero.  A count past the
// largest count of a View, or with bytes that do not fit in size_t, ends the
// process.  The strict forms report the operands of a product or a rounding
// that does not fit.  Complexity: O(1).
template <class T, std::size_t Align>
[[nodiscard]] constexpr ::foundation::alloc::Layout heap_array_layout_(std::size_t count) noexcept {
    require_heap_array_count_<T>(count);
    std::size_t const bytes = strict_align_up(strict_mul(count, sizeof(T)), Align);
    return ::foundation::alloc::mint_layout(ByteCount{bytes}, ::foundation::alloc::Alignment{Align})
        .expect("the storage of at least one element of a HeapArray is a layout");
}

// Ends each of the count elements at first, from the last, and gives their
// storage back to the allocation door with the bytes and the alignment of
// the mint.  Null storage holds no element.  Complexity: O(count).
template <class T, std::size_t Align>
constexpr void release_heap_elements_(T* first, std::size_t count) noexcept {
    if (first == nullptr) {
        return;
    }
    if constexpr (!std::is_trivially_destructible_v<T>) {
        for (std::size_t position = count; position != 0; --position) {
            first[position - 1].~T();
        }
    }
    heap_release_(static_cast<void*>(first), heap_array_layout_<T, Align>(count));
}

namespace owner {

// The fields of a HeapArray: the first element and the count, or null and
// zero when it holds no storage.
template <class T, std::size_t Align>
struct HeapArrayState {
    using owner_state_mark = OwnerStateMark;
    using released_element = T;

    T* first = nullptr;
    std::size_t count = 0;

    [[nodiscard]] static constexpr HeapArrayState emptied(HeapArrayState const& /*state*/) noexcept {
        return HeapArrayState{};
    }

    // Ends the elements and gives the storage back.  The test of null
    // storage always inlines, so the release of a state that a move or a take
    // emptied folds to no code in its caller, such as a move assignment into
    // an emptied array.  The body of the release can stay out of line.
    CRUCIBLE_INLINE static constexpr void release(HeapArrayState const& state) noexcept {
        if (state.first != nullptr) {
            release_heap_elements_<T, Align>(state.first, state.count);
        }
    }
};

// The fields of a MaskedArray: the first element, the count and the mask, or
// null and zeros when it holds no storage.
template <class T, std::size_t Align>
struct MaskedArrayState {
    using owner_state_mark = OwnerStateMark;
    using released_element = T;

    T* first = nullptr;
    std::size_t count = 0;
    std::size_t mask = 0;

    [[nodiscard]] static constexpr MaskedArrayState emptied(MaskedArrayState const& /*state*/) noexcept {
        return MaskedArrayState{};
    }

    static constexpr void release(MaskedArrayState const& state) noexcept {
        release_heap_elements_<T, Align>(state.first, state.count);
    }
};

}  // namespace owner

// The door of the mints of a HeapArray and of mint_masked_array, and the key
// of the constructors of a HeapArray, a MaskedArray and a WriteLease.
// HeapArrayDoor is the one friend of the key, and only the mints call the
// door.  The door and the key hold no template, so no explicit
// specialization of a template reaches the key.  A constructor of an array
// that takes the key allocates the storage and builds each element itself.
// So a use of namespace detail outside the base gets only an array of new
// storage, with no allocation capability at its call site.
struct HeapArrayDoor;

class [[nodiscard]] HeapArrayKey final {
    friend struct HeapArrayDoor;

    constexpr HeapArrayKey() noexcept {}
    constexpr HeapArrayKey(HeapArrayKey const&) noexcept {}

public:
    HeapArrayKey& operator=(HeapArrayKey const&) = delete("a key of a HeapArray lives for one construction");
};

struct HeapArrayDoor {
    [[nodiscard]] static constexpr HeapArrayKey key() noexcept { return HeapArrayKey{}; }
};

// The tags of the keyed constructors of a HeapArray, one for each mint
// after mint_heap_array.
struct BuildEachTag final {};
struct CopyTag final {};
struct ZeroTag final {};
struct WriteTag final {};

// The storage of count elements of T at the alignment Align, from the
// allocation door.  count is above zero, and a count whose bytes do not fit
// ends the process before the allocation.  The bytes hold no value.  The check
// of the count inlines into each constructor, so GCC bounds the loop of the
// constructor that writes the storage also when the layout is a call.  A
// constant count past the largest View then reaches no write of the loop.
template <class T, std::size_t Align>
[[nodiscard]] CRUCIBLE_INLINE void* allocate_heap_elements_(std::size_t count) noexcept {
    require_heap_array_count_<T>(count);
    return heap_allocate_(heap_array_layout_<T, Align>(count));
}

// The fatal exit of an exchange whose element put a value into the array while
// the array held no storage.
[[noreturn]] CRUCIBLE_COLD inline void report_heap_array_changed_() noexcept {
    fatal("the code of an element changed a HeapArray while an exchange moved an element");
}

// The fatal exit of an exchange of a MaskedArray whose element put a value
// into the array while the array held no storage.
[[noreturn]] CRUCIBLE_COLD inline void report_masked_array_changed_() noexcept {
    fatal("the code of an element changed a MaskedArray while an exchange moved an element");
}

// The fatal exit of an update of a HeapArray at a position at or past its
// count: before the body, or after a body that made the array shorter through
// a second name of the array.
[[noreturn]] CRUCIBLE_COLD inline void report_heap_update_past_count_(std::size_t position,
                                                                      std::size_t count) noexcept {
    fatal("an update at the position {} of a HeapArray of {} elements", position, count);
}

// The frame of one call of update of an owner of heap storage (a HeapArray or
// a MaskedArray): the copy of the element and the place of the element.  The
// body gets the copy, so no reference into the storage lives while it runs.
// The body can reach the owner through a second name and replace its storage.
// So the destructor finds the slot again through the owner (its private
// slot_of_update_, which names this frame as a friend), and puts the copy back
// there.  The result of the body exists before the destructor runs.  The copy
// and the put-back run no code of the element (HeapUpdatableElement), so no
// code reaches the owner while the frame names a slot of it.
template <class T, class Owner, class Place>
    requires HeapUpdatableElement<T>
class HeapUpdateFrame final {
    Owner& owner_;
    Place place_;
    T held_;
    [[no_unique_address]] LostUpdateOracle<T> oracle_;

public:
    constexpr HeapUpdateFrame(Owner& owner, Place place, T const& slot) noexcept
        : owner_{owner}, place_{place}, held_(slot), oracle_{slot} {}
    HeapUpdateFrame(HeapUpdateFrame const&) = delete("the frame of update puts its copy back one time");
    HeapUpdateFrame& operator=(HeapUpdateFrame const&) = delete("the frame of update puts its copy back one time");
    // An element with no assignment ends, and the copy starts in its place.
    // Its end and its move are trivial (PutBackElement), so no code of the
    // element runs while the slot holds no element.
    constexpr ~HeapUpdateFrame() {
        T& slot = owner_.slot_of_update_(place_);
        oracle_.require_unwritten(slot);
        if constexpr (std::is_nothrow_move_assignable_v<T>) {
            slot = static_cast<T&&>(held_);
        } else {
            slot.~T();
            ::new(static_cast<void*>(__builtin_launder(__builtin_addressof(slot)))) T(static_cast<T&&>(held_));
        }
    }
    // The call of the body with the copy.  It gives the result of the body.
    template <class Body>
    constexpr auto run(Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, T&>) {
        return static_cast<Body&&>(body)(held_);
    }
};

// Ends the moved-from element of a slot of an owner of heap storage, and
// starts value in its place.  The exchange of an element whose move and end
// run no code of a caller (MoveRunsCode) calls it, so no code reaches the
// owner between the end and the start.  Complexity: O(1).
template <class T>
CRUCIBLE_INLINE constexpr void restart_moved_slot_(T* slot, T&& value) noexcept {
    slot->~T();
    ::new(static_cast<void*>(__builtin_launder(slot))) T(static_cast<T&&>(value));
}

// The fatal exit of a writer of mint_heap_array_written that left elements
// with no write, or that gave elements past the last one.
[[noreturn]] CRUCIBLE_COLD inline void refuse_written_array_(std::size_t count, std::size_t unwritten,
                                                             std::size_t refused) noexcept {
    fatal("the writer of mint_heap_array_written left {} of {} elements with no write, and gave {} elements past "
          "the last one",
          unwritten, count, refused);
}

}  // namespace detail

// The arguments of mint_heap_array and mint_heap_array_copy: an element that
// copies with no throw, an alignment of the storage, and the allocation
// capability.
template <class T, std::size_t Align, class Marker>
concept CanMintHeapArray = OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && AllocationCapability<Marker>
                        && std::is_nothrow_copy_constructible_v<T>;

// The arguments of mint_heap_array_with: an alignment of the storage, the
// allocation capability, and an init that gives a T for each position with
// no throw.  The result is a T and not a type that converts to T, so the
// mint builds each element from it in place, with no copy and no move.  An
// element that does not copy or move, such as an Atomic, passes.  init holds
// no linear value by value (HoldsNoLinearValue of foundation/Linear.h): the
// mint calls it for each position and ends it, and a call is no consume.
template <class T, std::size_t Align, class Marker, class Init>
concept CanMintHeapArrayWith =
    OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && AllocationCapability<Marker>
    && std::is_nothrow_invocable_v<Init&, std::size_t> && std::same_as<std::invoke_result_t<Init&, std::size_t>, T>
    && ::foundation::linear::HoldsNoLinearValue<Init>;

// The arguments of mint_heap_array_zeroed: an element whose lifetime starts
// over its bytes (foundation/Lifetime.h), and of which each bit pattern is a
// value (BitValid), so zero in each byte is a value.  A bool, an enumeration
// with an enumerator, a pointer and a proof do not pass.
template <class T, std::size_t Align, class Marker>
concept CanMintHeapArrayZeroed = OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && AllocationCapability<Marker>
                              && ::foundation::lifetime::ImplicitLifetimeThroughout<T> && BitValid<T>;

// ── WriteLease ─────────────────────────────────────────────────────────────

// The write-only lease of the fresh elements of mint_heap_array_written.
// The lease writes the elements in order, from the first, and each element
// one time.  It has no read: no view(), no get() and no reference to an
// element.  So the writer reads no element that has no write.  The mint
// gives the writer a lease, and it gives the HeapArray only after the
// writer wrote each element.
//
//   put(value)      writes value into the next element
//   put_run(run)    writes the elements of a View into the next elements
//   remaining()     the count of elements with no write
//
// A write past the last element writes nothing, and the mint then ends the
// process when the writer returns.  The lease does not copy and does not
// move, so it stays in the frame of the mint.
template <class T>
    requires RegionCopyElement<T>
class [[nodiscard]] WriteLease final {
    T* next_ = nullptr;
    std::size_t remaining_ = 0;
    std::size_t refused_ = 0;
    // The seal refuses a lease from a byte image, so only the constructor
    // makes one.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    // The count elements at first, with the key that only HeapArrayDoor
    // builds.  first is not null when count is above zero.
    constexpr WriteLease(detail::HeapArrayKey const& /*key*/, T* first, std::size_t count) noexcept
        : next_{first}, remaining_{count} {}

    WriteLease(WriteLease const&) = delete("a WriteLease lends the elements of one mint, and a copy would write them "
                                           "two times");
    WriteLease& operator=(WriteLease const&) = delete("a WriteLease lends the elements of one mint");

    [[nodiscard]] constexpr std::size_t remaining() const noexcept { return remaining_; }

    // The count of elements that a write past the last element gave.
    [[nodiscard]] constexpr std::size_t refused() const noexcept { return refused_; }

    // Writes value into the next element.  Complexity: O(1).
    void put(T const& value) & noexcept {
        if (remaining_ == 0) [[unlikely]] {
            ++refused_;
            return;
        }
        *next_ = value;
        ++next_;
        --remaining_;
    }

    // Writes each element of run into the next elements, in order: one
    // move of bytes.  run is no part of this lease, because the array does
    // not exist before the mint returns.  A run longer than remaining()
    // writes nothing.  Complexity: O(run.size()).
    void put_run(View<T const> run) & noexcept {
        std::size_t const count = run.size();
        if (count > remaining_) [[unlikely]] {
            refused_ += count;
            return;
        }
        if (count != 0) {
            __builtin_memmove(static_cast<void*>(next_), first_of_(detail::BaseLayerRead{}, run), count * sizeof(T));
        }
        next_ += count;
        remaining_ -= count;
    }
};

// The arguments of mint_heap_array_written: an element whose lifetime starts
// over its bytes, and that a move of bytes writes (RegionCopyElement).  The
// writer takes the WriteLease with no throw, and holds no linear value by
// value (HoldsNoLinearValue of foundation/Linear.h): the mint calls it and
// ends it, and a call is no consume.  The lease is a local of the mint, so
// the writer stores no borrow outside the call (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h).
template <class T, std::size_t Align, class Marker, class Writer>
concept CanMintHeapArrayWritten = OwnedElement<T> && detail::HeapArrayAlignment<T, Align>
                               && AllocationCapability<Marker> && ::foundation::lifetime::ImplicitLifetimeThroughout<T>
                               && RegionCopyElement<T> && std::is_nothrow_invocable_v<Writer&, WriteLease<T>&>
                               && ::foundation::linear::HoldsNoLinearValue<Writer> && StoresNoBorrowOutside<Writer>;

// A HeapArray owns its elements alone, so it moves between threads as its
// element does (foundation/Transfer.h).
template <class T, std::size_t Align = alignof(T)>
    requires OwnedElement<T> && detail::HeapArrayAlignment<T, Align>
class [[nodiscard]][[= ::foundation::linear::linear_when<::foundation::linear::is_linear_type(^^T)>]]
                   [[= ::foundation::transfer::by_arguments{}]][[= equal_parts{}]] HeapArray final
    : public OwnerCore<detail::owner::HeapArrayState<T, Align>, HeapArray<T, Align>> {
    using State = detail::owner::HeapArrayState<T, Align>;
    using Core = OwnerCore<State, HeapArray>;
    // The key of the run, for the frame door frame_run_, a friend.
    using Core::run_key_;

    template <class Element, class Owner, class Place>
        requires HeapUpdatableElement<Element>
    friend class detail::HeapUpdateFrame;

    template <class Element, ::foundation::brand::IsBrand Brand, std::size_t ElementAlign>
        requires KeyedHolderParts<Element, Brand, ElementAlign>
    friend class KeyedHeapArray;

    // The body of get with the brand that the caller gives: the fresh brand of
    // the call of get, or of the call of get of a keyed holder of this array.
    template <::foundation::brand::IsBrand Brand>
    [[nodiscard]] CRUCIBLE_INLINE constexpr Option<Ref<T const, Brand>>
    element_of_(std::size_t position) const noexcept {
        return ArrayRefDoor::element_at_<Brand>(static_cast<T const*>(this->state_.first), this->state_.count,
                                                position);
    }

    // The slot at position for the frame of update, before and after its
    // body.  A position at or past the count of this time ends the process.
    [[nodiscard]] constexpr T& slot_of_update_(std::size_t position) noexcept {
        if (position >= this->state_.count) [[unlikely]] {
            detail::report_heap_update_past_count_(position, this->state_.count);
        }
        return this->state_.first[position];
    }

public:
    using element_type = T;

    // The empty array: no element and no storage.
    constexpr HeapArray() noexcept = default;

    // count copies of fill in new storage at the alignment Align, with the
    // key that only HeapArrayDoor builds.  A count of zero gives the empty
    // array and allocates nothing.  The allocation door never gives a null
    // pointer, and a count whose bytes do not fit ends the process before the
    // allocation.  The loop runs a cursor to the end of the storage, as the
    // loop of the BuildEach constructor does.  Complexity: one allocation and
    // O(count) copies.
    explicit HeapArray(detail::HeapArrayKey const& /*key*/, std::size_t count, T const& fill) noexcept
        requires std::is_nothrow_copy_constructible_v<T>
    {
        if (count == 0) {
            return;
        }
        T* const first = static_cast<T*>(detail::allocate_heap_elements_<T, Align>(count));
        T* const end = first + count;
        for (T* slot = first; slot != end; ++slot) {
            ::new(static_cast<void*>(slot)) T(fill);
        }
        this->state_ = State{first, count};
    }

    // count elements, each built in place from init(position), in the order
    // of the positions.  The result of init is a T, so the element takes it
    // with no copy and no move.  The loop runs a cursor to the end of the
    // storage, so GCC bounds each write by the storage of the allocation
    // door.  With an index loop, -Wstringop-overflow at -O3 sees a write past
    // the storage on a path that does not run.  Complexity: one allocation
    // and O(count) calls of init.
    template <class Init>
        requires std::is_nothrow_invocable_v<Init&, std::size_t>
              && std::same_as<std::invoke_result_t<Init&, std::size_t>, T>
    explicit HeapArray(detail::HeapArrayKey const& /*key*/, detail::BuildEachTag /*tag*/, std::size_t count,
                       Init& init) noexcept {
        if (count == 0) {
            return;
        }
        T* const first = static_cast<T*>(detail::allocate_heap_elements_<T, Align>(count));
        T* const end = first + count;
        std::size_t position = 0;
        for (T* slot = first; slot != end; ++slot) {
            ::new(static_cast<void*>(slot)) T(init(position));
            ++position;
        }
        this->state_ = State{first, count};
    }

    // A copy of each element of source, in order.  Complexity: one
    // allocation and O(source.size()) copies.
    explicit HeapArray(detail::HeapArrayKey const& /*key*/, detail::CopyTag /*tag*/, View<T const> source) noexcept
        requires std::is_nothrow_copy_constructible_v<T>
    {
        std::size_t const count = source.size();
        if (count == 0) {
            return;
        }
        T* const first = static_cast<T*>(detail::allocate_heap_elements_<T, Align>(count));
        T* target = first;
        for (T const& element : source) {
            ::new(static_cast<void*>(target)) T(element);
            ++target;
        }
        this->state_ = State{first, count};
    }

    // count elements with zero in each byte.  The allocation door gives
    // storage of the allocator of the C library, which starts the lifetime
    // of each element of an implicit-lifetime type with no write.  Each bit
    // pattern of T is a value, so each element holds the value of its zero
    // bytes.  Complexity: one allocation and one write of count * sizeof(T)
    // bytes.
    explicit HeapArray(detail::HeapArrayKey const& /*key*/, detail::ZeroTag /*tag*/, std::size_t count) noexcept
        requires ::foundation::lifetime::ImplicitLifetimeThroughout<T> && BitValid<T>
    {
        if (count == 0) {
            return;
        }
        void* const storage = detail::allocate_heap_elements_<T, Align>(count);
        __builtin_memset(storage, 0, count * sizeof(T));
        this->state_ = State{static_cast<T*>(storage), count};
    }

    // count elements that writer(lease) writes through a WriteLease.  The
    // allocator starts the lifetime of each element before the writer runs,
    // as for the zeroed array, and the lease writes each element before any
    // code can read it.  The writer runs also for a count of zero, with a
    // lease of no element.  A writer that leaves an element with no write, or
    // that gives an element past the last one, ends the process.  The
    // storage goes back to the allocation door first, and each element is
    // trivially destructible.  The check is a plain branch, so it holds under
    // each contract semantic.  Complexity: one allocation and the writes of
    // the writer.
    template <class Writer>
        requires ::foundation::lifetime::ImplicitLifetimeThroughout<T> && RegionCopyElement<T>
              && std::is_nothrow_invocable_v<Writer&, WriteLease<T>&>
    explicit HeapArray(detail::HeapArrayKey const& key, detail::WriteTag /*tag*/, std::size_t count,
                       Writer& writer) noexcept {
        T* first = nullptr;
        if (count != 0) {
            first = static_cast<T*>(detail::allocate_heap_elements_<T, Align>(count));
        }
        WriteLease<T> lease{key, first, count};
        writer(lease);
        if (lease.remaining() != 0 || lease.refused() != 0) [[unlikely]] {
            detail::release_heap_elements_<T, Align>(first, count);
            detail::refuse_written_array_(count, lease.remaining(), lease.refused());
        }
        this->state_ = State{first, count};
    }

    // The core of foundation/core/Owner.h gives the moves and the end of an
    // array.  The assignment ends each element that this array holds after
    // it takes other, so other can live in one of those elements.  The
    // assignment drops the elements, so an array of a linear element has
    // none.
    constexpr HeapArray(HeapArray&&) noexcept = default;
    constexpr HeapArray& operator=(HeapArray&&) noexcept
        requires DroppablePayload<T>
    = default;
    HeapArray& operator=(HeapArray&&)
        requires(!DroppablePayload<T>)
    = delete("an assignment drops the elements that the HeapArray holds, and a linear element must not be dropped: "
             "build a new HeapArray instead");

    HeapArray(HeapArray const&) = delete("a HeapArray owns its storage alone: a copy would free the storage two times");
    HeapArray&
    operator=(HeapArray const&) = delete("a HeapArray owns its storage alone: a copy would free the storage two times");

    [[nodiscard]] constexpr std::size_t size() const noexcept { return this->state_.count; }

    // The View of the const elements.  It borrows this array.  The mint
    // bounds the count and gives storage for each count above zero, so the
    // View comes through the key of an owner on the core, with no check.  An
    // assume of these two facts on the members gave the loop of the probe
    // heap_array_sum two checks and a spill to the stack.  A write takes
    // with_element for one element, or the frame door with_mut for the run,
    // so no two Views of one array write the same elements.
    [[nodiscard]] constexpr View<T const> view() const& noexcept {
        return View<T const>{this->run_key_(), this->state_.first, this->state_.count};
    }
    void view() && = delete("a view of a temporary HeapArray points into storage that the full expression frees");
    void view() const&& = delete("a view of a temporary HeapArray points into storage that the full expression frees");

    // The loop `for (T const& element : array)`.
    [[nodiscard]] constexpr ViewCursor<T const> begin() const& noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<T const> end() const& noexcept { return view().end(); }
    void begin() && = delete("a cursor of a temporary HeapArray points into storage that the full expression frees");
    void end() && = delete("a cursor of a temporary HeapArray points into storage that the full expression frees");
    void
    begin() const&& = delete("a cursor of a temporary HeapArray points into storage that the full expression frees");
    void end() const&& = delete("a cursor of a temporary HeapArray points into storage that the full expression frees");

    // The element at position, for a read, as an Option of a Ref with the
    // fresh brand of the call, or no value at or past the count: one compare.
    // The pack refuses each explicit argument, and the result type holds the
    // brand in undeducible_brand_t.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(std::size_t position) const& noexcept {
        return element_of_<Fresh>(position);
    }
    void get(std::size_t) && = delete("an element of a temporary HeapArray dangles at the end of the full expression");
    void
    get(std::size_t) const&& = delete("an element of a temporary HeapArray dangles at the end of the full expression");

    // The run of the elements for the frame door with_mut: a View that writes
    // them.  The key of a frame keeps each caller outside the base out, so
    // only a frame door gives a View that writes the elements of an array.
    [[nodiscard]] friend constexpr View<T> frame_run_(detail::FrameKey const& /*key*/, HeapArray& array) noexcept {
        return View<T>{HeapArray::run_key_(), array.state_.first, array.state_.count};
    }

    // Ends the elements while the array still names its storage, for an owner that
    // promises N elements to its end and tests no pointer (FixedHeapArray).  The
    // array is then empty.  The key of a frame keeps each caller outside the base
    // out.  Complexity: O(size()).
    friend constexpr void end_in_place_(detail::FrameKey const& /*key*/, HeapArray& array) noexcept {
        State::release(array.state_);
        array.state_ = State{};
    }

    // The frame door of one element (the element door): on_some gets the
    // element at position in place, for the time of the call, and at or past
    // size() on_none runs.  The two arms give the same type, which holds no
    // borrow, so no reference to the element leaves the door.  A const array
    // gives a const element.  No type holds the count of a HeapArray, so the
    // position is a count, as for get().  Complexity: one compare, and the
    // cost of the arm.
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T&> && std::is_invocable_v<OnNone>
              && std::same_as<std::invoke_result_t<OnSome, T&>, std::invoke_result_t<OnNone>>
              && HoldsNoBorrow<std::invoke_result_t<OnNone>> && ::foundation::linear::HoldsNoLinearValue<OnSome>
              && ::foundation::linear::HoldsNoLinearValue<OnNone> && ArmsStoreNoBorrowOutside<OnSome, OnNone>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<OnNone>
        with_element(std::size_t position, OnSome&& on_some,
                     OnNone&& on_none) & noexcept(std::is_nothrow_invocable_v<OnSome, T&>
                                                  && std::is_nothrow_invocable_v<OnNone>) {
        if (position >= this->state_.count) {
            return static_cast<OnNone&&>(on_none)();
        }
        return static_cast<OnSome&&>(on_some)(this->state_.first[position]);
    }
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T const&> && std::is_invocable_v<OnNone>
              && std::same_as<std::invoke_result_t<OnSome, T const&>, std::invoke_result_t<OnNone>>
              && HoldsNoBorrow<std::invoke_result_t<OnNone>> && ::foundation::linear::HoldsNoLinearValue<OnSome>
              && ::foundation::linear::HoldsNoLinearValue<OnNone> && ArmsStoreNoBorrowOutside<OnSome, OnNone>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<OnNone>
        with_element(std::size_t position, OnSome&& on_some,
                     OnNone&& on_none) const& noexcept(std::is_nothrow_invocable_v<OnSome, T const&>
                                                       && std::is_nothrow_invocable_v<OnNone>) {
        if (position >= this->state_.count) {
            return static_cast<OnNone&&>(on_none)();
        }
        return static_cast<OnSome&&>(on_some)(static_cast<T const*>(this->state_.first)[position]);
    }
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T&> && std::is_invocable_v<OnNone>
                  && (!::foundation::linear::HoldsNoLinearValue<OnSome>
                      || !::foundation::linear::HoldsNoLinearValue<OnNone>)
    void with_element(std::size_t position, OnSome&& on_some,
                      OnNone&& on_none) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class OnSome, class OnNone>
        requires std::is_invocable_v<OnSome, T const&> && std::is_invocable_v<OnNone>
                  && (!::foundation::linear::HoldsNoLinearValue<OnSome>
                      || !::foundation::linear::HoldsNoLinearValue<OnNone>)
    void with_element(std::size_t position, OnSome&& on_some,
                      OnNone&& on_none) const& = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class OnSome, class OnNone>
    void with_element(std::size_t position, OnSome&& on_some, OnNone&& on_none) && =
        delete("a frame door of a temporary HeapArray lends an element of storage that the full expression frees");
    template <class OnSome, class OnNone>
    void with_element(std::size_t position, OnSome&& on_some, OnNone&& on_none) const&& =
        delete("a frame door of a temporary HeapArray lends an element of storage that the full expression frees");

    // Moves value into the element at position, and gives back the element
    // that it held.  The door takes no body.  A body of the element door
    // that moves a value with a borrow into the element must capture that
    // value, and the store check of the door refuses such a capture.  The
    // position is below size(): no type holds the count of a HeapArray, so a
    // position at or past it ends the process.  An element whose move and
    // end run no code of a caller (MoveRunsCode of foundation/core/Owner.h)
    // moves in place: it moves out, the moved-from element ends in its slot,
    // and the value starts there.  MoveRunsCode reads no assignment, so the
    // door assigns only a trivial element and a bare owner.  The door inlines,
    // because the value doors run in the loops of each op.  Complexity: O(1).
    [[nodiscard]] CRUCIBLE_INLINE constexpr T exchange(std::size_t position, T value) & noexcept
        requires std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>
    {
        if (position >= this->state_.count) [[unlikely]] {
            fatal("an exchange at the position {} of a HeapArray of {} elements", position, this->state_.count);
        }
        if constexpr ((std::is_trivially_move_constructible_v<T> && std::is_trivially_move_assignable_v<T>)
                      || BareOwner<T>) {
            T held = std::move(this->state_.first[position]);
            this->state_.first[position] = std::move(value);
            return held;
        } else if constexpr (!MoveRunsCode<T>) {
            T held = static_cast<T&&>(this->state_.first[position]);
            detail::restart_moved_slot_(this->state_.first + position, static_cast<T&&>(value));
            return held;
        } else {
            // The move and the assignment of an element can reach the array
            // through a second name (the code of an element of
            // foundation/core/Owner.h).  So the array holds no storage while they
            // run, and a value that they put into it ends the process.
            State lent = this->take_state_();
            T held = std::move(lent.first[position]);
            lent.first[position] = std::move(value);
            if (this->state_.first != nullptr) [[unlikely]] {
                detail::report_heap_array_changed_();
            }
            this->state_ = lent;
            return held;
        }
    }
    void exchange(std::size_t position, T value) && =
        delete("an exchange in a temporary HeapArray writes storage that the full expression frees");
    void exchange(std::size_t position, T value) const& = delete("a const HeapArray has no writer of its elements");

    // The value doors of one element: set puts value at position and ends the
    // old element.  The door takes the value as an argument, so no code of the
    // caller runs while it writes.  It runs through exchange, so the code of
    // the element runs while the array holds no storage, and the old element
    // ends after the door.  A position at or past size() ends the process, as
    // for exchange.  Complexity: O(1), and the cost of the move and of the end.
    CRUCIBLE_INLINE constexpr void set(std::size_t position, T const& value) & noexcept
        requires std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_move_constructible_v<T>
              && std::is_nothrow_move_assignable_v<T> && DroppablePayload<T>
    {
        static_cast<void>(exchange(position, T(value)));
    }
    CRUCIBLE_INLINE constexpr void set(std::size_t position, T&& value) & noexcept
        requires std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T> && DroppablePayload<T>
    {
        static_cast<void>(exchange(position, static_cast<T&&>(value)));
    }
    void set(std::size_t position,
             T value) && = delete("a set in a temporary HeapArray writes storage that the full expression frees");
    void set(std::size_t position, T value) const& = delete("a const HeapArray has no writer of its elements");

    // The update door of one element: body gets a copy of the element at
    // position, and then the door puts the copy in the place of the element
    // (detail::HeapUpdateFrame).  The door gives the result of body, which
    // holds no borrow.  No reference into the storage lives while body runs,
    // so a second door of the array, through a helper or a second name, finds
    // whole elements.  Such a door can replace the storage, so the frame finds
    // the slot again after body, and a position at or past the count of that
    // time ends the process.  A nested update or a nested set of the same
    // element loses its write when the door puts the copy back.  That is a
    // wrong value and no memory error, and a build with the test oracles ends
    // the process then, as update of a FixedArray does.  The copy and the
    // put-back run while the array holds its storage, so the element is
    // trivially copyable (HeapUpdatableElement): no code of it runs there.
    // Complexity: O(1), and the cost of body, of one copy and of one move.
    template <class Body>
        requires HeapUpdatableElement<T> && std::is_invocable_v<Body, T&>
              && HoldsNoBorrow<detail::body_result_t<Body, T&>> && ::foundation::linear::HoldsNoLinearValue<Body>
              && StoresNoBorrowOutside<Body>
    [[= ::foundation::linear::borrow_match{}]]
        CRUCIBLE_INLINE constexpr auto update(std::size_t position,
                                              Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, T&>) {
        detail::HeapUpdateFrame<T, HeapArray, std::size_t> frame{*this, position, slot_of_update_(position)};
        return frame.run(static_cast<Body&&>(body));
    }
    template <class Body>
        requires LinearElementBody<Body, T>
    void update(std::size_t position, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires UpdatableElement<T> && (!std::is_trivially_copyable_v<T>)
                  && std::is_invocable_v<Body, T&> && ::foundation::linear::HoldsNoLinearValue<Body>
    void update(std::size_t position, Body&& body) & =
        delete("update of a HeapArray copies the element out of the storage and puts it back while the array holds the "
               "storage, and code of the copy or of the assignment could free the storage: take a trivially copyable "
               "element, or take exchange or set");
    template <class Body>
        requires(!CopiedOutElement<T>)
                 && std::is_invocable_v<Body, T&> && ::foundation::linear::HoldsNoLinearValue<Body>
    void update(std::size_t position, Body&& body) & = delete(
        "update gives the body a copy of the element and moves the copy back, so the element copies and moves with no "
        "throw: take an element that does not copy with exchange");
    template <class Body>
        requires CopiedOutElement<T> && (!PutBackElement<T>)
                  && std::is_invocable_v<Body, T&> && ::foundation::linear::HoldsNoLinearValue<Body>
    void update(std::size_t position, Body&& body) & = delete(
        "update of an element with no nothrow assignment ends the element in its slot and starts the copy there, so "
        "the move and the end of the element run no code: give the element a nothrow assignment, or take exchange");
    template <class Body>
    void update(std::size_t position, Body&& body) && =
        delete("an update in a temporary HeapArray writes storage that the full expression frees");
    template <class Body>
    void update(std::size_t position, Body&& body) const& = delete("a const HeapArray has no writer of its elements");

    // The iteration verbs of the View of the const elements.
    [[nodiscard]] constexpr auto reverse() const& noexcept { return view().reverse(); }
    void
    reverse() const&& = delete("a loop over a temporary HeapArray points into storage that the full expression frees");

    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent)
    [[nodiscard]] constexpr auto windows() const& noexcept {
        return view().template windows<Width>();
    }
    template <std::size_t Width>
    void
    windows() const&& = delete("a loop over a temporary HeapArray points into storage that the full expression frees");

    template <std::size_t Width>
        requires ViewExtent<T, Width> && (Width != dynamic_extent)
    [[nodiscard]] constexpr auto chunks() const& noexcept {
        return view().template chunks<Width>();
    }
    template <std::size_t Width>
    void
    chunks() const&& = delete("a loop over a temporary HeapArray points into storage that the full expression frees");

    // Two arrays are equal when they hold the same count of elements, and
    // each element equals the element at the same position of the other.
    // They order as the first pair of elements that differ, or as their
    // counts when one is a prefix of the other.  equal and order of the
    // Protocol family compare the elements.
    [[nodiscard]] friend constexpr bool operator==(HeapArray const& left, HeapArray const& right) noexcept
        requires Equatable<T>
    {
        return ::foundation::core::equal(left.view(), right.view());
    }
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(HeapArray const& left,
                                                                    HeapArray const& right) noexcept
        requires Orderable<T>
    {
        return ::foundation::core::order(left.view(), right.view());
    }
};

// The one door of a HeapArray: count copies of fill at the alignment Align,
// in new storage of the allocation door.  The allocation capability comes
// first, so each allocation shows at its call.  Complexity: one allocation
// and O(count) copies.
template <class T, std::size_t Align = alignof(T), class Marker>
    requires CanMintHeapArray<T, Align, Marker>
[[nodiscard]] HeapArray<T, Align> mint_heap_array(Marker /*allocation*/, std::size_t count, T const& fill) noexcept {
    return HeapArray<T, Align>{detail::HeapArrayDoor::key(), count, fill};
}

// count elements at the alignment Align, each built in place from
// init(position), in the order of the positions.  init gives a T with no
// throw, and no element moves after its construction.  Complexity: one
// allocation and O(count) calls of init.
template <class T, std::size_t Align = alignof(T), class Marker, class Init>
    requires CanMintHeapArrayWith<T, Align, Marker, Init>
[[nodiscard]] HeapArray<T, Align> mint_heap_array_with(Marker /*allocation*/, std::size_t count, Init init) noexcept {
    return HeapArray<T, Align>{detail::HeapArrayDoor::key(), detail::BuildEachTag{}, count, init};
}

// An init that holds a linear value by value binds to this overload, and a
// writer that holds one to the twin of mint_heap_array_written, so the
// refusal gives its reason (HoldsNoLinearValue of foundation/Linear.h).
template <class T, std::size_t Align = alignof(T), class Marker, class Init>
    requires AllocationCapability<Marker> && std::is_nothrow_invocable_v<Init&, std::size_t>
              && (!::foundation::linear::HoldsNoLinearValue<Init>)
void mint_heap_array_with(Marker allocation, std::size_t count, Init init) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// A copy of each element of source at the alignment Align.  Complexity: one
// allocation and O(source.size()) copies.
template <class T, std::size_t Align = alignof(T), class Marker>
    requires CanMintHeapArray<T, Align, Marker>
[[nodiscard]] HeapArray<T, Align> mint_heap_array_copy(Marker /*allocation*/,
                                                       std::type_identity_t<View<T const>> source) noexcept {
    return HeapArray<T, Align>{detail::HeapArrayDoor::key(), detail::CopyTag{}, source};
}

// count elements at the alignment Align with zero in each byte.  Each bit
// pattern of T is a value, so each element holds a value.  Complexity: one
// allocation and one write of the bytes.
template <class T, std::size_t Align = alignof(T), class Marker>
    requires CanMintHeapArrayZeroed<T, Align, Marker>
[[nodiscard]] HeapArray<T, Align> mint_heap_array_zeroed(Marker /*allocation*/, std::size_t count) noexcept {
    return HeapArray<T, Align>{detail::HeapArrayDoor::key(), detail::ZeroTag{}, count};
}

// count elements at the alignment Align that writer(lease) writes through a
// WriteLease, in order.  The lease has no read, so no element is read before
// its write.  A writer that leaves an element with no write, or that gives an
// element past the last one, ends the process, and the array then never
// exists.  Complexity: one allocation and the writes of the writer.
template <class T, std::size_t Align = alignof(T), class Marker, class Writer>
    requires CanMintHeapArrayWritten<T, Align, Marker, Writer>
[[nodiscard]] HeapArray<T, Align> mint_heap_array_written(Marker /*allocation*/, std::size_t count,
                                                          Writer writer) noexcept {
    return HeapArray<T, Align>{detail::HeapArrayDoor::key(), detail::WriteTag{}, count, writer};
}

template <class T, std::size_t Align = alignof(T), class Marker, class Writer>
    requires AllocationCapability<Marker> && std::is_nothrow_invocable_v<Writer&, WriteLease<T>&>
              && (!::foundation::linear::HoldsNoLinearValue<Writer>)
void mint_heap_array_written(Marker allocation, std::size_t count,
                             Writer writer) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// A writer that can store a borrow outside the call binds to this overload,
// so the refusal gives its reason.
template <class T, std::size_t Align = alignof(T), class Marker, class Writer>
    requires AllocationCapability<Marker> && std::is_nothrow_invocable_v<Writer&, WriteLease<T>&>
              && ::foundation::linear::HoldsNoLinearValue<Writer> && (!StoresNoBorrowOutside<Writer>)
void mint_heap_array_written(Marker allocation, std::size_t count,
                             Writer writer) = delete(CRUCIBLE_STORING_BODY_REFUSAL);

// ── MaskedArray ────────────────────────────────────────────────────────────

namespace detail {

// The largest count of a MaskedArray.  A slot below the count plus a probe
// step below the count then fits in 32 bits.
inline constexpr std::size_t masked_array_max_count = std::size_t{1} << 31;

// True for a power of two from 1 to masked_array_max_count.
[[nodiscard]] constexpr bool is_masked_array_count_(std::size_t count) noexcept {
    return count != 0 && count <= masked_array_max_count && (count & (count - 1)) == 0;
}

}  // namespace detail

// The arguments of mint_masked_array: an element that its default
// constructor builds with no throw, an alignment of the storage, and the
// allocation capability.  The mint copies no element, so an element that
// does not copy or move, such as a class of Atomic cells, passes.
template <class T, std::size_t Align, class Marker>
concept CanMintMaskedArray = OwnedElement<T> && detail::HeapArrayAlignment<T, Align> && AllocationCapability<Marker>
                          && std::is_nothrow_default_constructible_v<T>;

// An open-addressing table on the heap: a count of elements that is a power
// of two, at the alignment Align.  at_masked(key) reads the element at
// key & (count - 1) with no check, because the mask keeps each key below the
// count.  A probe loop adds its step to the key, and the mask wraps it.
//
// The array owns its storage alone.  A move gives the storage to the target
// and leaves the source empty, and the destructor ends each element and gives
// the storage back.  An empty array, the default one and a moved-from one,
// holds no storage.  at_masked of an empty array ends the process through
// fatal(), so no read goes through the null address.  The array owns its
// elements alone, so it moves between threads as its element does
// (foundation/Transfer.h).
template <class T, std::size_t Align = alignof(T)>
    requires OwnedElement<T> && detail::HeapArrayAlignment<T, Align>
class [[nodiscard]][[= ::foundation::linear::linear_when<::foundation::linear::is_linear_type(^^T)>]]
                   [[= ::foundation::transfer::by_arguments{}]] MaskedArray final
    : public OwnerCore<detail::owner::MaskedArrayState<T, Align>, MaskedArray<T, Align>> {
    using State = detail::owner::MaskedArrayState<T, Align>;
    using Core = OwnerCore<State, MaskedArray>;
    // The key of the run, for the frame door frame_run_, a friend.
    using Core::run_key_;

    // The first element.  A default or moved-from array holds no storage.
    // The use-after-move guard refuses a use of a moved-from local that it
    // can see, and each build tests the storage here.
    [[nodiscard]] constexpr T* held_() const noexcept {
        if (this->state_.first == nullptr) [[unlikely]] {
            fatal("an element of a MaskedArray that holds no storage: the array is default or moved from");
        }
        return this->state_.first;
    }

    template <class Element, class Owner, class Place>
        requires HeapUpdatableElement<Element>
    friend class detail::HeapUpdateFrame;

    // The slot at key & (count - 1) for the frame of update_masked, before and
    // after its body, through the storage and the mask of this time.
    [[nodiscard]] constexpr T& slot_of_update_(std::uint64_t key) noexcept { return held_()[key & this->state_.mask]; }

public:
    using element_type = T;

    // The empty array: no element and no storage.
    constexpr MaskedArray() noexcept = default;

    // count elements from their default constructor, in new storage at the
    // alignment Align, with the key that only HeapArrayDoor builds.  The mint
    // states the count as a precondition.  This check holds in each build
    // and under each contract semantic, because a count of zero gives the
    // mask of each bit, and at_masked would then read past the storage.
    // Complexity: one allocation and O(count) constructions.
    explicit MaskedArray(detail::HeapArrayKey const& /*key*/, std::size_t count) noexcept
        requires std::is_nothrow_default_constructible_v<T>
    {
        if (!detail::is_masked_array_count_(count)) [[unlikely]] {
            fatal("a MaskedArray of {} elements: the count must be a power of two from 1 to 2^31", count);
        }
        T* const first = static_cast<T*>(detail::heap_allocate_(detail::heap_array_layout_<T, Align>(count)));
        for (std::size_t position = 0; position != count; ++position) {
            ::new(static_cast<void*>(first + position)) T();
        }
        this->state_ = State{first, count, count - 1};
    }

    // The core of foundation/core/Owner.h gives the moves and the end of an
    // array.  The assignment ends each element that this array holds after
    // it takes other, so other can live in one of those elements.  The
    // assignment drops the elements, so an array of a linear element has
    // none.
    constexpr MaskedArray(MaskedArray&&) noexcept = default;
    constexpr MaskedArray& operator=(MaskedArray&&) noexcept
        requires DroppablePayload<T>
    = default;
    MaskedArray& operator=(MaskedArray&&)
        requires(!DroppablePayload<T>)
    = delete("an assignment drops the elements that the MaskedArray holds, and a linear element must not be dropped: "
             "build a new MaskedArray instead");

    MaskedArray(MaskedArray const&) =
        delete("a MaskedArray owns its storage alone: a copy would free the storage two times");
    MaskedArray& operator=(MaskedArray const&) =
        delete("a MaskedArray owns its storage alone: a copy would free the storage two times");

    [[nodiscard]] constexpr std::size_t size() const noexcept { return this->state_.count; }

    // The View of the const elements.  It borrows this array.  The mint bounds
    // the count and gives storage for each count, so the View comes through
    // the key of an owner on the core, with no check.  A write takes
    // with_masked for one element, or the frame door with_mut for the run.
    [[nodiscard]] constexpr View<T const> view() const& noexcept {
        return View<T const>{this->run_key_(), this->state_.first, this->state_.count};
    }
    void view() && = delete("a view of a temporary MaskedArray points into storage that the full expression frees");
    void
    view() const&& = delete("a view of a temporary MaskedArray points into storage that the full expression frees");

    // The run of the elements for the frame door with_mut, with the key of a
    // frame, as for a HeapArray.
    [[nodiscard]] friend constexpr View<T> frame_run_(detail::FrameKey const& /*key*/, MaskedArray& array) noexcept {
        return View<T>{MaskedArray::run_key_(), array.state_.first, array.state_.count};
    }

    // The const element at key & (count - 1), with no check of the key.  The
    // count is a power of two, so each masked key is below it.  An empty
    // array holds no storage, and each build tests that case here: one test
    // and one branch to a cold call.  Complexity: O(1).
    [[nodiscard]] constexpr T const& at_masked(std::uint64_t key) const& noexcept {
        return held_()[key & this->state_.mask];
    }
    void at_masked(std::uint64_t) && = delete("an element of a temporary MaskedArray dangles at the end of the full "
                                              "expression");
    void at_masked(std::uint64_t) const&& = delete("an element of a temporary MaskedArray dangles at the end of the "
                                                   "full expression");

    // The element door of a masked key (the element door): body gets the
    // element at key & (count - 1) in place, as T&, for the time of the call,
    // and the door gives the result of body.  The mask keeps each key below
    // the count, so the door has no none arm.  The result holds no borrow, so
    // no reference to the element leaves the door.  A const array gives a
    // const element.  The door writes no state of the array, so two threads
    // can call it on one shared array, and only an element that is safe for
    // concurrent writes, such as a class of Atomic cells, makes such calls
    // correct.  An empty array ends the process, as at_masked does.
    // Complexity: O(1), and the cost of body.
    template <class Body>
        requires std::is_invocable_v<Body, T&> && HoldsNoBorrow<std::invoke_result_t<Body, T&>>
              && ::foundation::linear::HoldsNoLinearValue<Body> && StoresNoBorrowOutside<Body>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<Body, T&>
        with_masked(std::uint64_t key, Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, T&>) {
        return static_cast<Body&&>(body)(held_()[key & this->state_.mask]);
    }
    template <class Body>
        requires std::is_invocable_v<Body, T const&> && HoldsNoBorrow<std::invoke_result_t<Body, T const&>>
              && ::foundation::linear::HoldsNoLinearValue<Body> && StoresNoBorrowOutside<Body>
    [[= ::foundation::linear::borrow_match{}]] constexpr std::invoke_result_t<Body, T const&>
        with_masked(std::uint64_t key, Body&& body) const& noexcept(std::is_nothrow_invocable_v<Body, T const&>) {
        return static_cast<Body&&>(body)(static_cast<T const*>(held_())[key & this->state_.mask]);
    }
    template <class Body>
        requires LinearElementBody<Body, T>
    void with_masked(std::uint64_t key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires LinearElementBody<Body, T const>
    void with_masked(std::uint64_t key, Body&& body) const& = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
    void with_masked(std::uint64_t key, Body&& body) && =
        delete("a frame door of a temporary MaskedArray lends an element of storage that the full expression frees");
    template <class Body>
    void with_masked(std::uint64_t key, Body&& body) const&& =
        delete("a frame door of a temporary MaskedArray lends an element of storage that the full expression frees");

    // Moves value into the element at key & (count - 1), and gives back the
    // element that it held.  The door takes no body.  The move and the
    // assignment of an element with code can reach the array through a second
    // name (the code of an element of foundation/core/Owner.h), so the array
    // holds no storage while they run, and a value that they put into it ends
    // the process.  An element whose move and end run no code of a caller
    // (MoveRunsCode) moves in place, in the form of exchange of a HeapArray.
    // An empty array ends the process, as at_masked does.  The door inlines,
    // because the value doors run in the loops of each op.  Complexity: O(1).
    [[nodiscard]] CRUCIBLE_INLINE constexpr T exchange_masked(std::uint64_t key, T value) & noexcept
        requires std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>
    {
        T* const first = held_();
        std::uint64_t const position = key & this->state_.mask;
        if constexpr ((std::is_trivially_move_constructible_v<T> && std::is_trivially_move_assignable_v<T>)
                      || BareOwner<T>) {
            T held = std::move(first[position]);
            first[position] = std::move(value);
            return held;
        } else if constexpr (!MoveRunsCode<T>) {
            T held = static_cast<T&&>(first[position]);
            detail::restart_moved_slot_(first + position, static_cast<T&&>(value));
            return held;
        } else {
            State lent = this->take_state_();
            T held = std::move(lent.first[position]);
            lent.first[position] = std::move(value);
            if (this->state_.first != nullptr) [[unlikely]] {
                detail::report_masked_array_changed_();
            }
            this->state_ = lent;
            return held;
        }
    }
    void exchange_masked(std::uint64_t key, T value) && =
        delete("an exchange in a temporary MaskedArray writes storage that the full expression frees");
    void exchange_masked(std::uint64_t key,
                         T value) const& = delete("a const MaskedArray has no writer of its elements");

    // The value doors of one element: set_masked puts value at key & (count -
    // 1) and ends the old element, through exchange_masked.  The door takes
    // the value as an argument, so no code of the caller runs while it writes.
    // Complexity: O(1), and the cost of the move and of the end.
    CRUCIBLE_INLINE constexpr void set_masked(std::uint64_t key, T const& value) & noexcept
        requires std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_move_constructible_v<T>
              && std::is_nothrow_move_assignable_v<T> && DroppablePayload<T>
    {
        static_cast<void>(exchange_masked(key, T(value)));
    }
    CRUCIBLE_INLINE constexpr void set_masked(std::uint64_t key, T&& value) & noexcept
        requires std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T> && DroppablePayload<T>
    {
        static_cast<void>(exchange_masked(key, static_cast<T&&>(value)));
    }
    void set_masked(std::uint64_t key, T value) && =
        delete("a set in a temporary MaskedArray writes storage that the full expression frees");
    void set_masked(std::uint64_t key, T value) const& = delete("a const MaskedArray has no writer of its elements");

    // The update door of one element: body gets a copy of the element at
    // key & (count - 1), and then the door puts the copy in the place of the
    // element (detail::HeapUpdateFrame).  No reference into the storage lives
    // while body runs, so a second door of the array finds whole elements.
    // Such a door can replace the storage, so the frame finds the slot again
    // after body, through the mask of that time.  The door gives the result of
    // body, which holds no borrow.  A nested update or a nested set of the same
    // element loses its write: a wrong value and no memory error, which a
    // build with the test oracles ends.  The element is trivially copyable, as
    // for update of a HeapArray.  An empty array ends the process, as
    // at_masked does.  Complexity: O(1), and the cost of body, of one copy and
    // of one move.
    template <class Body>
        requires HeapUpdatableElement<T> && std::is_invocable_v<Body, T&>
              && HoldsNoBorrow<detail::body_result_t<Body, T&>> && ::foundation::linear::HoldsNoLinearValue<Body>
              && StoresNoBorrowOutside<Body>
    [[= ::foundation::linear::borrow_match{}]]
        CRUCIBLE_INLINE constexpr auto update_masked(std::uint64_t key,
                                                     Body&& body) & noexcept(std::is_nothrow_invocable_v<Body, T&>) {
        detail::HeapUpdateFrame<T, MaskedArray, std::uint64_t> frame{*this, key, slot_of_update_(key)};
        return frame.run(static_cast<Body&&>(body));
    }
    template <class Body>
        requires LinearElementBody<Body, T>
    void update_masked(std::uint64_t key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires UpdatableElement<T> && (!std::is_trivially_copyable_v<T>)
                  && std::is_invocable_v<Body, T&> && ::foundation::linear::HoldsNoLinearValue<Body>
    void update_masked(std::uint64_t key, Body&& body) & = delete(
        "update_masked copies the element out of the storage and puts it back while the array holds the storage, and "
        "code of the copy or of the assignment could free the storage: take a trivially copyable element, or take "
        "exchange_masked or set_masked");
    template <class Body>
        requires(!CopiedOutElement<T>)
                 && std::is_invocable_v<Body, T&> && ::foundation::linear::HoldsNoLinearValue<Body>
    void update_masked(std::uint64_t key, Body&& body) & = delete(
        "update_masked gives the body a copy of the element and moves the copy back, so the element copies and moves "
        "with no throw: take an element that does not copy with exchange_masked");
    template <class Body>
        requires CopiedOutElement<T> && (!PutBackElement<T>)
                  && std::is_invocable_v<Body, T&> && ::foundation::linear::HoldsNoLinearValue<Body>
    void update_masked(std::uint64_t key, Body&& body) & = delete(
        "update_masked of an element with no nothrow assignment ends the element in its slot and starts the copy "
        "there, so the move and the end of the element run no code: give the element a nothrow assignment, or take "
        "exchange_masked");
    template <class Body>
    void update_masked(std::uint64_t key, Body&& body) && =
        delete("an update in a temporary MaskedArray writes storage that the full expression frees");
    template <class Body>
    void update_masked(std::uint64_t key,
                       Body&& body) const& = delete("a const MaskedArray has no writer of its elements");
};

// The one door of a MaskedArray: count elements from their default
// constructor, at the alignment Align, in new storage of the allocation
// door.  The count is a power of two from 1 to 2^31.  The allocation
// capability comes first, so each allocation shows at its call.  The
// allocation door ends the process when the memory is exhausted, as it does
// for mint_heap_array.  Complexity: one allocation and O(count) constructions.
template <class T, std::size_t Align = alignof(T), class Marker>
    requires CanMintMaskedArray<T, Align, Marker>
[[nodiscard]] MaskedArray<T, Align> mint_masked_array(Marker /*allocation*/, std::size_t count) noexcept {
    // Not CRUCIBLE_PRE: its assumption under the ignore semantic could delete the check of the constructor.
    contract_assert(detail::is_masked_array_count_(count));
    return MaskedArray<T, Align>{detail::HeapArrayDoor::key(), count};
}

// ── The frame doors ────────────────────────────────────────────────────────

// The frame door of a heap array.  The array moves into the frame, body gets
// the ViewMut of its elements, and the door gives the array back: alone for a
// body that gives void, and after the result in a Record for each other body.
// The pack refuses each explicit argument, so no caller names the brand of
// the frame.  A HeapArray and a MaskedArray move a pointer and a count in and
// out.  Complexity: O(1), and the cost of the body.
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh>
    requires NamesNoArgument<Refused...> && MovedOwner<Owner> && ArrayFrameOwner<Owner>
          && FrameBody<Body, typename Owner::element_type, Fresh, detail::frame_extent_v<Owner>>
[[nodiscard]] constexpr detail::frame_return_t<
    detail::body_result_t<Body&&, ViewMut<typename Owner::element_type, Fresh, detail::frame_extent_v<Owner>>>, Owner>
with_mut(Owner&& owner, Body&& body) noexcept {
    Owner held{static_cast<Owner&&>(owner)};
    View<typename Owner::element_type, detail::frame_extent_v<Owner>> const run =
        frame_run_(ArrayFrameKeyDoor::key_(), held);
    return detail::give_run_back_<Fresh>(ArrayFrameKeyDoor::key_(), static_cast<Owner&&>(held), run,
                                         static_cast<Body&&>(body));
}

// A body that is no frame callable binds to this overload, the refused twin
// (FrameBody of foundation/core/Region.h), so the refusal gives its reason.
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires NamesNoArgument<Refused...> && MovedOwner<Owner> && ArrayFrameOwner<Owner> && FrameGates<Body>
void with_mut(Owner&& owner, Body&& body) = delete(CRUCIBLE_FRAME_BODY_REFUSAL);
template <class... Refused, class Owner, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && (!std::is_reference_v<Owner>) && (!std::is_const_v<Owner>)
             && ArrayFrameOwner<Owner>
             && LinearFrameBody<Body, typename Owner::element_type, Fresh, detail::frame_extent_v<Owner>>
void with_mut(Owner&& owner, Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// A named array stays with the caller while the body runs, so the body could
// read or write it beside the ViewMut through a capture.  This twin refuses
// a named array and a const one, which cannot move into the frame.
template <class Owner, class Body>
    requires(std::is_lvalue_reference_v<Owner> || std::is_const_v<std::remove_reference_t<Owner>>)
             && ArrayFrameOwner<std::remove_cvref_t<Owner>>
void with_mut(Owner&& owner, Body&& body) = delete(
    "with_mut takes the owner into its frame and gives it back: pass std::move(owner), and keep the owner that the "
    "call returns");

}  // namespace foundation::core

namespace foundation::lifetime {

// Starts the lifetime of count objects of type T over the storage, and gives
// the address of the first object.  The storage must hold count * sizeof(T)
// bytes aligned for T.  The element is const when the storage or T is const.
// The address is no View: a View of the objects comes from the form of
// start_as_array over a View of bytes (foundation/core/Region.h), or from the
// owner of the storage.  The asm statement of the start is
// detail::start_storage_ of foundation/core/Region.h, whose comment tells how
// a start works.
//
// An object starts only at an address that is aligned for its type, and the
// count and the storage must give a run that a View can hold.  So the function
// ends the process for a storage that is not aligned for T, for a count past
// max_view_count(sizeof(T)) and for a null storage with a count, in each build
// and under each contract semantic.  The three checks are
// CRUCIBLE_FATAL_INVARIANT, whose failure path does not return.  A contract
// check is not used, because its handler returns under the observe semantic.
// GCC then keeps each load after a start of an unknown alignment, and a test
// of the asm statement would see no start with no statement.  The address of a
// local of a known alignment folds the check of the alignment.  The checks
// read the storage before the asm statement, because the statement hides the
// pointer from the optimizer.
//
// Volatile storage is refused, because a View holds no volatile element.  A
// caller that reads through volatile converts the address of an element to a
// pointer to volatile, which is an implicit conversion.  An array type T is
// refused for the same reason: a caller starts the lifetime of its elements.
template <ImplicitLifetimeThroughout T, typename Storage>
    requires(!std::is_volatile_v<Storage> && ::foundation::core::ViewElement<element_for_storage_t<Storage, T>>)
[[nodiscard]] CRUCIBLE_INLINE element_for_storage_t<Storage, T>* start_as_array(Storage* storage,
                                                                                std::size_t count) noexcept {
    using Element = element_for_storage_t<Storage, T>;
    CRUCIBLE_FATAL_INVARIANT((__builtin_bit_cast(std::uintptr_t, storage) & (alignof(Element) - 1)) == 0);
    CRUCIBLE_FATAL_INVARIANT(count <= ::foundation::core::max_view_count(sizeof(Element)));
    CRUCIBLE_FATAL_INVARIANT(storage != nullptr || count == 0);
    return detail::start_storage_<T>(storage, count);
}

}  // namespace foundation::lifetime
