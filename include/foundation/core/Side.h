#pragma once

// The side of a value: the role of one argument of a call, as a type.
//
//   Side<Tag, T>          a value of T in the role Tag.  Two tags give two
//                         types, so two arguments of one payload type in two
//                         roles cannot swap at a call
//   side_as<Tag>(value)   the Side of the value in the role Tag
//   value()               a const reference to the payload, of a Side that a
//                         name holds
//   take()                the payload, by value, of an rvalue Side: the form
//                         of a payload that only moves
//
// A tag is an empty final class, which the user of a role declares in a
// namespace side of its own layer, such as foundation::core::side::LeftFactor
// of foundation/core/Product.h.  The tag names the role, and it holds no
// value.
//
// Rules
//   A Side is no aggregate: its one field is private, and the explicit
//   constructor and side_as are the only ways to make one.  It has no
//   implicit conversion to its payload or from it, no comparison and no
//   arithmetic.  So a call site states the role of each argument, and a
//   function that takes two Sides of two tags refuses the two arguments in
//   the other order.  The check file of this header holds each rule, and the
//   fixtures neg_core_side_* hold each refusal.
//
// Cost
//   A Side has the size, the alignment and the layout of its payload, and it
//   is trivially copyable when its payload is.  So the ABI passes it as it
//   passes the payload.  A callee that takes the Side by reference and reads
//   value() compiles to the instructions of a callee that takes the payload,
//   as the matrix product of foundation/core/Product.h shows.  A chain of
//   calls that take the Side by value can copy the payload at each call.
//
// The strong-type carrier of the base takes Side as its axis of the role of
// an argument.  The owner reviews that carrier before it lands.

#include <type_traits>
#include <utility>

namespace foundation::core {

// The roles of the base.  Each header that names a role declares its tag
// here, and each layer above declares its tags in a namespace side of its
// own.
namespace side {}

// A tag of a role: an empty final class.
template <class Tag>
concept SideTag = std::is_class_v<Tag> && std::is_empty_v<Tag> && std::is_final_v<Tag>;

// A payload of a Side: an object type that is not an array, not const, not
// volatile, and moves and ends with no throw.
template <class T>
concept SidePayload = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                   && std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>;

template <class Tag, class T>
    requires SideTag<Tag> && SidePayload<T>
class Side final {
public:
    using tag_type = Tag;
    using value_type = T;

    explicit constexpr Side(T value) noexcept : value_{static_cast<T&&>(value)} {}

    // The payload of a Side that a name holds.
    [[nodiscard]] constexpr T const& value() const& noexcept { return value_; }
    void value() const&& = delete("a reference into a temporary Side dangles at the end of the full expression: use "
                                  "take()");

    // The payload, by value.  The Side is an rvalue, so no name reads it
    // after the move.
    [[nodiscard]] constexpr T take() && noexcept { return static_cast<T&&>(value_); }

private:
    T value_;
};

// The Side of the value in the role Tag.  The pack takes no explicit
// argument after Tag, so a call states only the role.
template <class Tag, class... Refused, class T>
    requires(sizeof...(Refused) == 0) && SideTag<Tag> && SidePayload<std::remove_cvref_t<T>>
         && std::is_nothrow_constructible_v<std::remove_cvref_t<T>, T&&>
[[nodiscard]] constexpr Side<Tag, std::remove_cvref_t<T>> side_as(T&& value) noexcept {
    return Side<Tag, std::remove_cvref_t<T>>{std::remove_cvref_t<T>{static_cast<T&&>(value)}};
}

}  // namespace foundation::core
