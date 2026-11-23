#pragma once

// The Seat family: a holder that is always full, for the value of a loop.
//
//   Seat<T>                  one T, from its mint.  No Seat is empty, so no
//                            door of a Seat tests a state
//   mint_seat(value)         a Seat that holds value
//   seat.step(fn)            one turn of a loop: fn takes the payload as an
//                            rvalue and gives the next payload, of the type T,
//                            and the Seat holds it in the same storage
//   std::move(seat).take()   the payload, out of a Seat that the caller gives
//                            up
//
// Step
//   A loop over a linear value, such as a session handle that refuses move
//   assignment, steps the value and keeps the next value in the same place.
//   An Option can hold the value, but each turn takes the payload out, steps
//   it and fills the Option again.  That is two more moves of the payload and
//   a test of the state at each door.  A Seat holds its payload from its mint
//   to its take, so its type proves that it is full, and step() tests nothing.
//   step() calls fn on the payload in place, ends the old payload, and builds
//   the next payload in the same storage from the result of fn.  That is one
//   move of the payload for each turn, as the emplace of a std::optional
//   gives.  The payload stays at one address for the whole loop, so a step of
//   a session handle sees no change of its holder.
//
// Rules
//   fn gives T itself, so a step cannot change the type of the payload, and fn
//   does not throw, so the storage never stays without a payload.  A Seat has
//   no default constructor, no copy and no assignment.  It moves, so a linear
//   payload makes the Seat linear, and the rule must-consume holds a Seat to
//   its take.  A step and a take of a temporary or of a const Seat are deleted
//   with their reasons.  The cost of a step is the call of fn, the end of the
//   old payload and one move of the next payload.

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>

#include <concepts>
#include <new>
#include <type_traits>

namespace foundation::core {

// A payload of a Seat: an object type that is not an array, not const and not
// volatile, whose move and end do not throw.  So a step never leaves the
// storage without a payload.
template <class T>
concept SeatPayload = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                   && std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>;

// One turn of a loop over a payload T: a call on the payload as an rvalue,
// which does not throw and gives the next payload of the type T.
template <class Fn, class T>
concept StepOf = std::is_nothrow_invocable_v<Fn&, T&&> && std::same_as<std::invoke_result_t<Fn&, T&&>, T>;

template <SeatPayload T>
class Seat;

template <SeatPayload T>
[[nodiscard]] constexpr Seat<T> mint_seat(T value) noexcept;

template <SeatPayload T>
class [[nodiscard]][[= ::foundation::linear::linear_when<::foundation::linear::is_linear_type(^^T)>]]
                   [[= ::foundation::lifetime::no_start_over_bytes{}]] Seat final {
public:
    // The key of the constructor of a Seat.  Only mint_seat builds it, so each
    // Seat comes through the mint.  Its two constructors are user-provided, so
    // the key is not trivially copyable, and no copy of bytes builds one.
    class MintKey final {
        constexpr MintKey() noexcept {}
        constexpr MintKey(MintKey const&) noexcept {}

        friend constexpr Seat mint_seat<T>(T value) noexcept;

    public:
        MintKey& operator=(MintKey const&) = delete("a key of a Seat lives for one construction");
    };

private:
    // The payload is a plain member.  step() ends it through end_payload_ and
    // builds the next payload of the same type in its storage, which then
    // replaces the old member, and the destructor of the Seat ends the
    // member that the storage holds.
    T payload_;

    // The end of the old payload in a step, before the next payload takes its
    // storage.  It is the drop door of the Seat: the payload that it ends is
    // the moved rest of the payload that the step gave to fn.
    [[= ::foundation::linear::drop_door{}]] constexpr void end_payload_() noexcept { payload_.~T(); }

    constexpr Seat(MintKey, T&& value) noexcept : payload_(static_cast<T&&>(value)) {}

    friend constexpr Seat mint_seat<T>(T value) noexcept;

public:
    Seat() = delete("a Seat is full from its mint: build it with mint_seat(value)");
    Seat(Seat const&) = delete("a Seat holds one payload, and a copy would give the payload a second holder");
    Seat&
    operator=(Seat const&) = delete("a Seat holds one payload, and a copy would give the payload a second holder");
    Seat& operator=(Seat&&) = delete("a Seat keeps its payload in its own storage: step the payload, or take it out");

    // A move of a Seat moves its payload into the new Seat.  The old Seat then
    // holds the moved payload, and its end ends that payload.
    constexpr Seat(Seat&& source) noexcept : payload_(static_cast<T&&>(source.payload_)) {}

    constexpr ~Seat() = default;

    // One turn of the loop: fn takes the payload as an rvalue, and the Seat
    // holds the payload that fn gives, in the same storage (the step).
    // fn holds no linear value by value (HoldsNoLinearValue of
    // foundation/Linear.h): the step calls it and ends it, and a call is no
    // consume.
    template <class Fn>
        requires StepOf<Fn, T> && ::foundation::linear::HoldsNoLinearValue<Fn>
    constexpr void step(Fn&& fn) & noexcept {
        // The storage of the payload, before fn moves the payload.
        void* const storage = static_cast<void*>(__builtin_addressof(payload_));
        T next = fn(static_cast<T&&>(payload_));
        end_payload_();
        ::new(storage) T(static_cast<T&&>(next));
    }

    // A function that holds a linear value by value binds to this overload,
    // so the refusal gives its reason.
    template <class Fn>
        requires StepOf<Fn, T> && (!::foundation::linear::HoldsNoLinearValue<Fn>)
    void step(Fn&&) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    // The deleted forms take the constraint of step too, so a function of
    // another type gets the refusal of the constraint and not the reason of
    // a deleted form.
    template <class Fn>
        requires StepOf<Fn, T>
    void step(Fn&&) && = delete("a step of a temporary Seat builds a payload that the full expression ends: name the "
                                "Seat");
    template <class Fn>
        requires StepOf<Fn, T>
    void step(Fn&&) const& = delete("a step changes the payload, and a const Seat keeps its payload");
    template <class Fn>
        requires StepOf<Fn, T>
    void step(Fn&&) const&& = delete("a step changes the payload, and a const Seat keeps its payload");

    // The payload, out of a Seat that the caller gives up.
    [[nodiscard]] constexpr T take() && noexcept { return static_cast<T&&>(payload_); }

    T take() & = delete("take() of a Seat that the caller keeps would leave a moved payload in it: use "
                        "std::move(seat).take()");
    T take() const& = delete("take() moves the payload out, and a const Seat keeps its payload");
    T take() const&& = delete("take() moves the payload out, and a const Seat keeps its payload");
};

// A Seat that holds value.  The mint is the one door to a Seat, so each Seat
// is full from its start.
template <SeatPayload T>
[[nodiscard]] constexpr Seat<T> mint_seat(T value) noexcept {
    return Seat<T>{typename Seat<T>::MintKey{}, static_cast<T&&>(value)};
}

}  // namespace foundation::core
