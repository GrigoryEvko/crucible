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

#include <foundation/Platform.h>

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <string_view>
#include <type_traits>

namespace fixy {

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace row_discipline {
struct saturated;
}  // namespace row_discipline

template <typename T>
    requires std::is_arithmetic_v<T>
class [[nodiscard]] Saturated {
public:
    using value_type = T;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::Saturated"; }
    using row_discipline = ::fixy::row_discipline::saturated;
    using row_payload = T;

private:
    T value_ = T{};
    bool clamped_ = false;

public:
    constexpr Saturated() noexcept = default;

    // Wrapping a plain value implicitly is safe, because it can only
    // mean that no clamping was observed.  Stating the flag explicitly
    // is a claim about an operation that already happened, and the
    // constructor below asks the caller to spell it out.
    constexpr Saturated(T v) noexcept : value_{v}, clamped_{false} {}

    constexpr explicit Saturated(T v, bool was_clamped) noexcept : value_{v}, clamped_{was_clamped} {}

    constexpr Saturated(Saturated const&) = default;
    constexpr Saturated(Saturated&&) = default;
    constexpr Saturated& operator=(Saturated const&) = default;
    constexpr Saturated& operator=(Saturated&&) = default;
    ~Saturated() = default;

    [[nodiscard]] constexpr T const& value() const& noexcept { return value_; }
    [[nodiscard]] constexpr T value() && noexcept { return value_; }

    [[nodiscard]] constexpr bool was_clamped() const noexcept { return clamped_; }

    // Unwrapping drops the observation, so it stays explicit.
    [[nodiscard]] constexpr explicit operator T() const noexcept { return value_; }

    // The flag takes part in the comparison.  A value that arrived by
    // clean arithmetic is not equal to the same value that arrived by
    // clamping.
    [[nodiscard]] friend constexpr bool operator==(Saturated const& a, Saturated const& b) noexcept = default;
};

// The three below split the work: the builtin answers whether the exact
// result fits, and the library supplies the clamped value when it does
// not.  A clamp with a sign rule written out by hand is a chance to get
// the end of the range wrong, for a value the library already knows.
// This header adds only the part the library does not carry: the flag.

template <std::integral T>
[[nodiscard]] constexpr Saturated<T> add_sat_checked(T a, T b) noexcept {
    T r{};
    if (__builtin_add_overflow(a, b, &r)) [[unlikely]] {
        return Saturated<T>{std::saturating_add(a, b), true};
    }
    return Saturated<T>{r, false};
}

template <std::integral T>
[[nodiscard]] constexpr Saturated<T> sub_sat_checked(T a, T b) noexcept {
    T r{};
    if (__builtin_sub_overflow(a, b, &r)) [[unlikely]] {
        return Saturated<T>{std::saturating_sub(a, b), true};
    }
    return Saturated<T>{r, false};
}

template <std::integral T>
[[nodiscard]] constexpr Saturated<T> mul_sat_checked(T a, T b) noexcept {
    T r{};
    if (__builtin_mul_overflow(a, b, &r)) [[unlikely]] {
        return Saturated<T>{std::saturating_mul(a, b), true};
    }
    return Saturated<T>{r, false};
}

}  // namespace fixy
