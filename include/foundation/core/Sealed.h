#pragma once

// The sealed object of the Ref family: an object that no code changes after
// its construction, so a thread beside its owner can read it.
//
//   Sealed<T>           one object of T, const from its construction to its
//                       end.  It does not copy, move or take an assignment
//
// Operations
//   Sealed sealed{std::move(object)}
//                       takes the object by move.  T comes from the argument.
//                       mint_box<Sealed<T>>(alloc, std::move(object)) seals
//                       an object on the heap, behind a Box that moves
//   *sealed, sealed->   the const object.  A temporary gives none
//
//   ::fixy::SegmentList<Event> log;
//   ...
//   ::fixy::Sealed const sealed{std::move(log)};
//   // A body of run_beside can hold a reference to sealed and read the log,
//   // while the owner reads it too.
//
// Reader beside the owner
//   The caller of run_joined waits for its bodies, so a body can read an owner
//   through a reference to const.  Beside the caller, such as in run_beside, a
//   reference to const proves nothing: the caller can append to a SegmentList
//   that the body reads.  So the doors ask for an object that two threads can
//   write at the same time (Synchronized of foundation/Transfer.h).  A Sealed
//   is such an object: its one member is const, so no write reaches it, and it
//   has no operation that changes it.  A move would write the object that a
//   reader reads, so a Sealed does not move.
//
//   The type of T must be Shareable.  A SmallList writes its lend count in a
//   read, so it is not Shareable, and it does not seal.
//
// A Sealed ends its object at its own end, with no consume, so a linear object
// does not seal.

#include <foundation/Transfer.h>
#include <foundation/core/Choice.h>

#include <concepts>
#include <type_traits>
#include <utility>

namespace foundation::core {

// An object that a Sealed can hold: an object type that moves with no throw
// and that two threads can read at the same time.  The end of a Sealed drops
// its object, so a linear object is refused.
template <class T>
concept SealableObject = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                      && std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>
                      && DroppablePayload<T> && ::foundation::transfer::Shareable<T>;

template <SealableObject T>
class [[nodiscard]] Sealed final {
    // The const member makes the class Synchronized: no write reaches it.
    T const object_;

public:
    using element_type = T;

    // Takes the object.  The parameter is no template parameter of its own, so
    // only an rvalue of T seals, and the caller names the move.
    constexpr explicit Sealed(T&& object) noexcept : object_{static_cast<T&&>(object)} {}

    Sealed(Sealed const&) = delete("a Sealed does not copy and does not move: a move would write the object that a "
                                   "reader on another thread reads.  Move a Box of the Sealed instead");
    Sealed& operator=(Sealed const&) = delete("a Sealed takes no assignment: it would end the object that a reader on "
                                              "another thread reads");

    // The const object.
    [[nodiscard]] constexpr T const& operator*() const& noexcept { return object_; }
    void operator*() && = delete("the object of a temporary Sealed dangles at the end of the full expression");
    void operator*() const&& = delete("the object of a temporary Sealed dangles at the end of the full expression");

    [[nodiscard]] constexpr T const* operator->() const& noexcept { return __builtin_addressof(object_); }
    void operator->() && = delete("the object of a temporary Sealed dangles at the end of the full expression");
    void operator->() const&& = delete("the object of a temporary Sealed dangles at the end of the full expression");
};

}  // namespace foundation::core
