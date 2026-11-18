#pragma once

// The one error of an append into a full owner of the Region family:
// FixedList, FixedText, SmallList, GrowList and GrowText.
//
//   Full<T>    the owner holds no more room, so the value of the append did
//              not become an element.  For a linear T it holds the value, so
//              the caller gets the value back.  For each other T it is empty,
//              and the value ends with the refusal
//
// A text gives Full<char>, also from format() of foundation/core/Format.h.
// This header sits below the owners, so a text header and a list header name
// one error type.

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>

namespace foundation::core {

// An empty Full ends the value with the refusal.
template <class T>
struct Full final {
    [[nodiscard]] friend constexpr bool operator==(Full, Full) noexcept = default;
};

// The Full of a linear value holds the value, so the caller of the append
// gets the value back and consumes it.  take() moves the value out of a Full
// that the caller gives up.  The move operations are those of the value, and
// a linear value has a user-provided move assignment, so the Full is not
// trivially copyable either.
template <class T>
    requires(::foundation::linear::is_linear_type(^^T))
class[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] Full<T> final {
    T value_;

public:
    constexpr explicit Full(T value) noexcept : value_{static_cast<T&&>(value)} {}

    constexpr Full(Full&&) noexcept = default;
    constexpr Full& operator=(Full&&) noexcept = default;
    Full(Full const&) = delete("a Full holds a linear value, and a copy of the Full would copy the value");
    Full& operator=(Full const&) = delete("a Full holds a linear value, and a copy of the Full would copy the value");
    constexpr ~Full() = default;

    [[nodiscard]] constexpr T take() && noexcept { return static_cast<T&&>(value_); }
    T take() & = delete("take() of a Full that the caller keeps would leave a moved value in it");
};

}  // namespace foundation::core
