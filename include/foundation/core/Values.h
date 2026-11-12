#pragma once

// The Scalar family: the exchange and the swap of two values.
//
//   exchange(slot, value)  puts value into slot, and gives the old value of
//                          slot
//   swap(left, right)      exchanges the values of two objects of one type
//
// The type moves with no throw, so no operation leaves a value half moved,
// and a type that does not move, such as a Pinned type, is refused.  A
// const object, a volatile object and an array are refused too.  A linear
// value moves through each operation with no drop: exchange gives the old
// value back, and its result is [[nodiscard]].
//
// exchange deduces its type from the slot only, so `exchange(count, 0)`
// converts the 0 to the type of the slot.  -Wconversion refuses a
// conversion that can change the value.
//
// swap is a function object and not a function.  Argument-dependent lookup
// never finds an object, so `using std::swap; swap(a, b)` does not reach
// this swap, and a call of this swap never reaches a swap that a unit
// declares for its own type.  The object is in an inline namespace, as the
// customization point objects of std::ranges are.  A hidden friend named
// swap in a class of the enclosing namespace then is no second declaration
// of a different kind of entity.

#include <concepts>
#include <type_traits>

namespace foundation::core {

// A value that exchange and swap move: an object type that is not an array
// and not cv-qualified, with a move constructor and a move assignment that
// do not throw.
template <class T>
concept Exchangeable = std::is_object_v<T> && !std::is_array_v<T> && std::same_as<T, std::remove_cv_t<T>>
                    && std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>;

template <Exchangeable T>
[[nodiscard]] constexpr T exchange(T& slot, std::type_identity_t<T> value) noexcept {
    T old = static_cast<T&&>(slot);
    slot = static_cast<T&&>(value);
    return old;
}

namespace detail {

// The type of the object swap.
struct SwapValues final {
    template <Exchangeable T>
    static constexpr void operator()(T& left, T& right) noexcept {
        T held = static_cast<T&&>(left);
        left = static_cast<T&&>(right);
        right = static_cast<T&&>(held);
    }
};

}  // namespace detail

inline namespace values {

inline constexpr detail::SwapValues swap{};

}  // namespace values

}  // namespace foundation::core
