#pragma once

// The Ref family: one object and its owner.
//
// Box<T> owns one object of type T on the heap.  The one door is
// mint_box<T>(alloc, args...): it allocates the storage, builds the object
// from the arguments, and gives the Box.  The first argument is the
// allocation capability of foundation/effects, so each allocation shows at
// its call site.  The destructor destroys the object and frees the
// storage with the same size and alignment, so a Box never leaks and never
// frees twice.
//
// A Box is move-only.  A move leaves the source with no object, and the
// use-after-move guard refuses a later use of that source.  A Box comes
// only from mint_box, so no pointer that a caller holds becomes a Box, and
// the destructor frees only storage that mint_box allocated.
//
// get(), operator* and operator-> give a borrow of the object.  The
// overloads for an rvalue are deleted, because a borrow of a temporary Box
// dangles at the end of the full expression.  consume() moves the object
// out of a Box that the caller gives up, and frees the storage.
//
// C++ keeps a moved-from object alive, and no type can refuse its use.  So
// each access tests for the object in each build, and a Box with no object
// ends the process through fatal().  The test is one compare with a branch
// to a cold call.  A loop that reads the object many times takes one
// borrow with get() before the loop.

#include <foundation/core/Report.h>

#include <concepts>
#include <cstddef>
#include <new>
#include <type_traits>

namespace foundation::core {

// The object of a Box: a complete object type that is not an array, not
// cv-qualified and not abstract, with a destructor that does not throw.
template <class T>
concept BoxPayload = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                  && !std::is_abstract_v<T> && std::is_nothrow_destructible_v<T>;

// The tag that shows an allocation at a call site.  foundation/effects is
// above this layer, so the gate reads the member that its allocation tag
// declares, and not the type.  The tag is empty and costs nothing.
template <class Marker>
concept AllocationCapability = std::is_empty_v<Marker> && std::is_trivially_copyable_v<Marker>
                            && std::same_as<typename Marker::grants_allocation, void>;

template <class T, class Marker, class... Args>
concept CanMintBox = BoxPayload<T> && AllocationCapability<Marker> && std::constructible_from<T, Args&&...>;

template <BoxPayload T>
class Box;

template <class T, class Marker, class... Args>
    requires CanMintBox<T, Marker, Args...>
[[nodiscard]] Box<T> mint_box(Marker allocation, Args&&... args) noexcept;  // MINT-PATTERN-OK: allocating

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
            ::operator delete(static_cast<void*>(object_), sizeof(T), std::align_val_t{alignof(T)});
            object_ = nullptr;
        }
    }

public:
    using element_type = T;

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

    // A moved-from or consumed Box holds no object.  The use-after-move
    // guard refuses a use of it that it can see, and each build tests it
    // here.
    [[nodiscard]] T& get() & noexcept {
        if (object_ == nullptr) [[unlikely]] {
            fatal("a borrow of a Box that holds no object: the Box was moved from or consumed");
        }
        return *object_;
    }
    [[nodiscard]] T const& get() const& noexcept {
        if (object_ == nullptr) [[unlikely]] {
            fatal("a borrow of a Box that holds no object: the Box was moved from or consumed");
        }
        return *object_;
    }
    [[nodiscard]] T& operator*() & noexcept { return get(); }
    [[nodiscard]] T const& operator*() const& noexcept { return get(); }
    [[nodiscard]] T* operator->() & noexcept { return &get(); }
    [[nodiscard]] T const* operator->() const& noexcept { return &get(); }

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

    T& get() && = delete("a borrow of a temporary Box dangles at the end of the full expression");
    T const& get() const&& = delete("a borrow of a temporary Box dangles at the end of the full expression");
    T& operator*() && = delete("a borrow of a temporary Box dangles at the end of the full expression");
    T const& operator*() const&& = delete("a borrow of a temporary Box dangles at the end of the full expression");
    T* operator->() && = delete("a borrow of a temporary Box dangles at the end of the full expression");
    T const* operator->() const&& = delete("a borrow of a temporary Box dangles at the end of the full expression");
};

// The storage has the size and the alignment of T, and the destructor of
// the Box frees it with the same two values.  An argument list that does
// not build a T, and a tag that is not the allocation capability, fail the
// gate at the call.
template <class T, class Marker, class... Args>
    requires CanMintBox<T, Marker, Args...>
[[nodiscard]] Box<T> mint_box(Marker /*allocation*/, Args&&... args) noexcept {  // MINT-PATTERN-OK: allocating
    void* const storage = ::operator new(sizeof(T), std::align_val_t{alignof(T)}, std::nothrow);
    if (storage == nullptr) [[unlikely]] {
        // The tree does not run where an exhausted memory can recover.
        fatal("mint_box could not allocate the storage of its object");
    }
    return Box<T>{::new(storage) T(static_cast<Args&&>(args)...)};
}

}  // namespace foundation::core
