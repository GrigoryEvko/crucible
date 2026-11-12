#pragma once

// The Ref family: one object and its owner.
//
// Ref<T> reaches one object of type T and is never null.  Ref<T>::to(object)
// is the one door from an object, and to() of a temporary is deleted,
// because the Ref dangles at the end of the full expression.  *ref and
// ref->member reach the object with no test, because a Ref has no empty
// state.  A Ref<T> converts to a Ref<T const>, and nothing converts it back.
// same_object(a, b) tells if two Refs reach one object.  A Ref has no ==, no
// default value and no conversion from a pointer.  Option<Ref<T>> has the
// size of one pointer: the null pointer is its empty value, and no Ref holds
// it.  A Ref owns nothing, so a Ref that outlives its object dangles.
// address_of(ref) gives the Address of the object, which answers
// is_aligned<N>() and ==, and gives no pointer back.
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
// use-after-move guard refuses a later use of that source.  A Box comes
// only from mint_box, so no pointer that a caller holds becomes a Box, and
// the destructor frees only storage that mint_box allocated.
//
// operator* and operator-> give a borrow of the object, and view() gives
// the borrow as a Ref.  The overloads for an rvalue are deleted, because a
// borrow of a temporary Box dangles at the end of the full expression.
// consume() moves the object out of a Box that the caller gives up, and
// frees the storage.
//
// C++ keeps a moved-from object alive, and no type can refuse its use.  So
// each access tests for the object in each build, and a Box with no object
// ends the process through fatal().  The test is one compare with a branch
// to a cold call.  A loop that reads the object many times takes one Ref
// with view() before the loop, and the Ref has no test.

#include <foundation/ByteSeal.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Report.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

namespace foundation::core {

// The object of a Ref: an object type that is not an array and not
// volatile.  An abstract class is a valid target, because a Ref builds
// nothing.
template <class T>
concept RefTarget = std::is_object_v<T> && !std::is_array_v<T> && !std::is_volatile_v<T>;

template <RefTarget T>
class Ref {
    T* object_ = nullptr;
    // The seal makes the assignment user-provided, so std::bit_cast cannot
    // build a Ref from the bytes of an address.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr explicit Ref(T* object) noexcept : object_{object} {}

    template <RefTarget U>
    friend class Ref;
    friend struct niche<Ref>;

public:
    [[nodiscard]] static constexpr Ref to(T& object) noexcept { return Ref{__builtin_addressof(object)}; }
    static Ref to(T&& object) = delete("a Ref of a temporary dangles at the end of the full expression");

    // Adds a const to the target.
    template <class U>
        requires std::same_as<U const, T> && (!std::is_const_v<U>)
    constexpr Ref(Ref<U> other) noexcept : object_{other.object_} {}

    [[nodiscard]] constexpr T& operator*() const noexcept { return *object_; }
    [[nodiscard]] constexpr T* operator->() const noexcept { return object_; }
};

// True when the two Refs reach one object.  The two targets are one type,
// with or without a const.
template <class First, class Second>
    requires std::same_as<std::remove_const_t<First>, std::remove_const_t<Second>>
[[nodiscard]] constexpr bool same_object(Ref<First> first, Ref<Second> second) noexcept {
    return __builtin_addressof(*first) == __builtin_addressof(*second);
}

class Address;

template <RefTarget T>
[[nodiscard]] Address address_of(Ref<T> ref) noexcept;

// The address of one object, which address_of(ref) gives.  It answers an
// alignment query and compares with a different address.  An address
// changes from run to run, so it has no hash and no order, and no pointer
// comes back from it.  The seal makes the assignment user-provided, so
// std::bit_cast cannot build an Address from an integer.
class Address {
    std::uintptr_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr explicit Address(std::uintptr_t value) noexcept : value_{value} {}

    template <RefTarget T>
    friend Address address_of(Ref<T> ref) noexcept;

public:
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
template <RefTarget T>
[[nodiscard]] Address address_of(Ref<T> ref) noexcept {
    return Address{__builtin_bit_cast(std::uintptr_t, __builtin_addressof(*ref))};
}

// The empty value of an Option of a Ref: the null pointer, which no Ref
// holds.  Option<Ref<T>> then has the size of one pointer.
template <RefTarget T>
struct niche<Ref<T>> {
    [[nodiscard]] static constexpr Ref<T> empty(niche_key<Ref<T>> const&) noexcept {
        return Ref<T>{static_cast<T*>(nullptr)};
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<Ref<T>> const&, Ref<T> const& ref) noexcept {
        return ref.object_ == nullptr;
    }
};

// The object of a Box: a complete object type that is not an array, not
// cv-qualified and not abstract, with a destructor that does not throw.
template <class T>
concept BoxPayload = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                  && !std::is_abstract_v<T> && std::is_nothrow_destructible_v<T>;

template <class T, class Marker, class... Args>
concept CanMintBox = BoxPayload<T> && AllocationCapability<Marker> && std::constructible_from<T, Args&&...>;

template <BoxPayload T>
class Box;

template <class T, class Marker, class... Args>
    requires CanMintBox<T, Marker, Args...>
[[nodiscard]] Box<T> mint_box(Marker allocation, Args&&... args) noexcept;

template <BoxPayload T>
class [[nodiscard]] Box {
    T* object_ = nullptr;

    explicit Box(T* object) noexcept : object_{object} {}

    template <class U, class Marker, class... Args>
        requires CanMintBox<U, Marker, Args...>
    friend Box<U> mint_box(Marker allocation, Args&&... args) noexcept;

    void destroy_() noexcept {
        if (object_ != nullptr) {
            object_->~T();
            detail::heap_release_(static_cast<void*>(object_), sizeof(T), alignof(T));
            object_ = nullptr;
        }
    }

    // A moved-from or consumed Box holds no object.  The use-after-move
    // guard refuses a use of it that it can see, and each build tests it
    // here.
    [[nodiscard]] T* held_() const noexcept {
        if (object_ == nullptr) [[unlikely]] {
            fatal("a borrow of a Box that holds no object: the Box was moved from or consumed");
        }
        return object_;
    }

public:
    Box(Box&& other) noexcept : object_{other.object_} { other.object_ = nullptr; }

    Box& operator=(Box&& other) noexcept {
        if (this != &other) {
            destroy_();
            object_ = other.object_;
            other.object_ = nullptr;
        }
        return *this;
    }

    Box(Box const&) = delete("a Box owns its object alone: a copy would free the object two times");
    Box& operator=(Box const&) = delete("a Box owns its object alone: a copy would free the object two times");

    ~Box() { destroy_(); }

    [[nodiscard]] T& operator*() & noexcept { return *held_(); }
    [[nodiscard]] T const& operator*() const& noexcept { return *held_(); }
    [[nodiscard]] T* operator->() & noexcept { return held_(); }
    [[nodiscard]] T const* operator->() const& noexcept { return held_(); }

    // The borrow of the whole object as a Ref.  The Ref has no test, so a
    // loop reads the object through it at the cost of a raw pointer.
    [[nodiscard]] Ref<T> view() & noexcept { return Ref<T>::to(*held_()); }
    [[nodiscard]] Ref<T const> view() const& noexcept { return Ref<T const>::to(*held_()); }

    // Moves the object out, destroys the moved-from object, and frees the
    // storage.  The Box then holds no object.
    [[nodiscard]] T consume() && noexcept
        requires std::is_nothrow_move_constructible_v<T>
    {
        if (object_ == nullptr) [[unlikely]] {
            fatal("consume of a Box that holds no object: the Box was moved from or consumed");
        }
        T taken(static_cast<T&&>(*object_));
        destroy_();
        return taken;
    }

    Ref<T> view() && = delete("a borrow of a temporary Box dangles at the end of the full expression");
    Ref<T const> view() const&& = delete("a borrow of a temporary Box dangles at the end of the full expression");
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
    void* const storage = detail::heap_allocate_(sizeof(T), alignof(T));
    return Box<T>{::new(storage) T(static_cast<Args&&>(args)...)};
}

}  // namespace foundation::core
