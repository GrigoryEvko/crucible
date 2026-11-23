#pragma once

// A value paired with the fact that the operation producing it clamped
// to the limits of its type.
//
// Plain saturating arithmetic returns only the clamped result and throws
// that fact away, leaving a caller unable to tell a genuine maximum from
// an overflow that landed on one.  Carrying the observation costs a
// single byte.
//
// The flag describes the value that the operation gave.  So the value is
// read-only after construction: no accessor gives a mutable reference.
// A caller that changes the value makes a new Saturated, and an
// assignment replaces the value and the flag together.
//
// Doors
//   exact(value)        a value that no clamp made.  No plain value converts
//                       in, so the claim shows at each call
//   clamped_at_max(), clamped_at_min()
//                       the highest or the lowest value of an integer type
//                       after a clamp.  A clamp gives a limit of the type, so
//                       no caller states a clamp of a different value
//   add_sat_checked, sub_sat_checked, mul_sat_checked
//                       the result of an operation and its flag.  Only these
//                       three build a value with a flag from the operation
//   then(next)          the result of a later step of a value: the value of
//                       next, with the flag set when either step clamped.  So
//                       a counter that clamped one time stays clamped

#include <foundation/ByteSeal.h>
#include <foundation/Platform.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Number.h>

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace fixy {

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace row_discipline {
struct saturated;
}  // namespace row_discipline

namespace detail::saturated {

// The word of the flag of a Saturated of T: an unsigned word of the size of
// T, so the flag fills the bytes after the value, and no byte of a Saturated
// lacks a value.  A copy then carries no padding byte that a constant
// evaluation or -ftrivial-auto-var-init writes through the stack.  A type
// wider than eight bytes keeps a bool and its padding.
template <std::size_t Size>
struct FlagWord {
    using type = bool;
};
template <>
struct FlagWord<1> {
    using type = std::uint8_t;
};
template <>
struct FlagWord<2> {
    using type = std::uint16_t;
};
template <>
struct FlagWord<4> {
    using type = std::uint32_t;
};
template <>
struct FlagWord<8> {
    using type = std::uint64_t;
};

}  // namespace detail::saturated

template <typename T>
    requires std::is_arithmetic_v<T>
class Saturated;

template <::foundation::core::Integer T>
[[nodiscard]] constexpr Saturated<T> add_sat_checked(T a, T b) noexcept;

template <::foundation::core::Integer T>
[[nodiscard]] constexpr Saturated<T> sub_sat_checked(T a, T b) noexcept;

template <::foundation::core::Integer T>
[[nodiscard]] constexpr Saturated<T> mul_sat_checked(T a, T b) noexcept;

template <typename T>
    requires std::is_arithmetic_v<T>
class [[nodiscard]] Saturated {
public:
    using value_type = T;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::Saturated"; }
    using row_discipline = ::fixy::row_discipline::saturated;
    using row_payload = T;

private:
    // The fact of a clamp, which only the doors of this header state.
    enum class Clamp : unsigned char {
        exact,
        clamped,
    };

    using Flag = typename detail::saturated::FlagWord<sizeof(T)>::type;

    T value_ = T{};
    Flag clamped_ = Flag{};
    // The seal closes the byte routes: std::bit_cast and a lifetime start over
    // bytes would give a flag that no door stated.  The copy constructors stay
    // trivial, so a Saturated still goes back in registers.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr Saturated(T value, Clamp clamp) noexcept
        : value_{value}, clamped_{clamp == Clamp::clamped ? static_cast<Flag>(1) : Flag{}} {}

    template <::foundation::core::Integer U>
    friend constexpr Saturated<U> add_sat_checked(U a, U b) noexcept;
    template <::foundation::core::Integer U>
    friend constexpr Saturated<U> sub_sat_checked(U a, U b) noexcept;
    template <::foundation::core::Integer U>
    friend constexpr Saturated<U> mul_sat_checked(U a, U b) noexcept;

public:
    // The exact zero.
    constexpr Saturated() noexcept = default;

    // A value that no clamp made.
    [[nodiscard]] static constexpr Saturated exact(T value) noexcept { return Saturated{value, Clamp::exact}; }

    // The highest and the lowest value of an integer type after a clamp.
    [[nodiscard]] static constexpr Saturated clamped_at_max() noexcept
        requires ::foundation::core::Integer<T>
    {
        return Saturated{::foundation::core::max_of<T>, Clamp::clamped};
    }
    [[nodiscard]] static constexpr Saturated clamped_at_min() noexcept
        requires ::foundation::core::Integer<T>
    {
        return Saturated{::foundation::core::min_of<T>, Clamp::clamped};
    }

    constexpr Saturated(Saturated const&) = default;
    constexpr Saturated(Saturated&&) = default;
    constexpr Saturated& operator=(Saturated const&) = default;
    constexpr Saturated& operator=(Saturated&&) = default;
    ~Saturated() = default;

    [[nodiscard]] constexpr T const& value() const& noexcept { return value_; }
    [[nodiscard]] constexpr T value() && noexcept { return value_; }

    [[nodiscard]] constexpr bool was_clamped() const noexcept { return clamped_ != Flag{}; }

    // The result of a later step of this value: the value of next, and the
    // flag set when this value or next clamped.  A flag only adds a clamp,
    // so no chain loses one.
    [[nodiscard]] constexpr Saturated then(Saturated const& next) const noexcept {
        return Saturated{next.value_, was_clamped() || next.was_clamped() ? Clamp::clamped : Clamp::exact};
    }

    // Unwrapping drops the observation, so it stays explicit.
    [[nodiscard]] constexpr explicit operator T() const noexcept { return value_; }

    // The flag takes part in the comparison.  A value that arrived by
    // clean arithmetic is not equal to the same value that arrived by
    // clamping.
    // A floating-point value has no compare by equality in this project.
    [[nodiscard]] friend constexpr bool operator==(Saturated const& a, Saturated const& b) noexcept
        requires(!std::is_floating_point_v<T>)
    {
        return a.value_ == b.value_ && a.clamped_ == b.clamped_;
    }
};

// The three below split the work: the builtin answers whether the exact
// result fits, and the saturating form of the Scalar family gives the
// clamped value when it does not.  This header adds only the part that the
// family does not carry: the flag.

template <::foundation::core::Integer T>
[[nodiscard]] constexpr Saturated<T> add_sat_checked(T a, T b) noexcept {
    T r{};
    if (__builtin_add_overflow(a, b, &r)) [[unlikely]] {
        return Saturated<T>{::foundation::core::saturating_add(a, b), Saturated<T>::Clamp::clamped};
    }
    return Saturated<T>{r, Saturated<T>::Clamp::exact};
}

template <::foundation::core::Integer T>
[[nodiscard]] constexpr Saturated<T> sub_sat_checked(T a, T b) noexcept {
    T r{};
    if (__builtin_sub_overflow(a, b, &r)) [[unlikely]] {
        return Saturated<T>{::foundation::core::saturating_sub(a, b), Saturated<T>::Clamp::clamped};
    }
    return Saturated<T>{r, Saturated<T>::Clamp::exact};
}

template <::foundation::core::Integer T>
[[nodiscard]] constexpr Saturated<T> mul_sat_checked(T a, T b) noexcept {
    T r{};
    if (__builtin_mul_overflow(a, b, &r)) [[unlikely]] {
        return Saturated<T>{::foundation::core::saturating_mul(a, b), Saturated<T>::Clamp::clamped};
    }
    return Saturated<T>{r, Saturated<T>::Clamp::exact};
}

}  // namespace fixy
