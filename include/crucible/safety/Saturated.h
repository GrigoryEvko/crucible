#pragma once

// A value paired with the fact that the operation producing it clamped
// to the limits of its type.
//
// Plain saturating arithmetic returns only the clamped result and throws
// that fact away, leaving a caller unable to tell a genuine maximum from
// an overflow that landed on one.  Carrying the observation costs a
// single byte.

#include <crucible/Platform.h>

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::safety {

template <typename T>
    requires std::is_arithmetic_v<T>
class [[nodiscard]] Saturated {
public:
    using value_type = T;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::Saturated"; }

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
    [[nodiscard]] constexpr T& value() & noexcept { return value_; }
    [[nodiscard]] constexpr T value() && noexcept { return value_; }

    [[nodiscard]] constexpr bool was_clamped() const noexcept { return clamped_; }

    // Unwrapping drops the observation, so it stays explicit.
    [[nodiscard]] constexpr explicit operator T() const noexcept { return value_; }

    // The flag takes part in the comparison.  A value that arrived by
    // clean arithmetic is not equal to the same value that arrived by
    // clamping.
    [[nodiscard]] friend constexpr bool operator==(Saturated const& a, Saturated const& b) noexcept = default;
};

template <std::integral T>
[[nodiscard]] constexpr Saturated<T> add_sat_checked(T a, T b) noexcept {
    T r{};
    if (__builtin_add_overflow(a, b, &r)) [[unlikely]] {
        if constexpr (std::is_signed_v<T>) {
            // A signed sum can only overflow when both operands share a
            // sign, so the sign of either one names the end it ran past.
            r = (b > T{0}) ? std::numeric_limits<T>::max() : std::numeric_limits<T>::min();
        } else {
            r = std::numeric_limits<T>::max();
        }
        return Saturated<T>{r, true};
    }
    return Saturated<T>{r, false};
}

template <std::integral T>
[[nodiscard]] constexpr Saturated<T> sub_sat_checked(T a, T b) noexcept {
    T r{};
    if (__builtin_sub_overflow(a, b, &r)) [[unlikely]] {
        if constexpr (std::is_signed_v<T>) {
            // The sense is opposite to addition: subtracting a positive
            // operand can only run past the minimum, and subtracting a
            // negative one can only run past the maximum.
            r = (b > T{0}) ? std::numeric_limits<T>::min() : std::numeric_limits<T>::max();
        } else {
            // An unsigned difference can only run below zero.
            r = T{0};
        }
        return Saturated<T>{r, true};
    }
    return Saturated<T>{r, false};
}

template <std::integral T>
[[nodiscard]] constexpr Saturated<T> mul_sat_checked(T a, T b) noexcept {
    T r{};
    if (__builtin_mul_overflow(a, b, &r)) [[unlikely]] {
        if constexpr (std::is_signed_v<T>) {
            // The product's sign decides the end it ran past.
            bool same_sign = (a < T{0}) == (b < T{0});
            r = same_sign ? std::numeric_limits<T>::max() : std::numeric_limits<T>::min();
        } else {
            r = std::numeric_limits<T>::max();
        }
        return Saturated<T>{r, true};
    }
    return Saturated<T>{r, false};
}

static_assert(sizeof(Saturated<uint8_t>) == 2);
static_assert(sizeof(Saturated<uint16_t>) == 4);
static_assert(sizeof(Saturated<uint32_t>) == 8);
static_assert(sizeof(Saturated<uint64_t>) == 16);
static_assert(sizeof(Saturated<int8_t>) == 2);
static_assert(sizeof(Saturated<int32_t>) == 8);
static_assert(sizeof(Saturated<int64_t>) == 16);

static_assert(alignof(Saturated<uint64_t>) == alignof(uint64_t));
static_assert(alignof(Saturated<uint32_t>) == alignof(uint32_t));

static_assert(std::is_trivially_copyable_v<Saturated<uint64_t>>);
static_assert(std::is_trivially_copyable_v<Saturated<int64_t>>);
static_assert(std::is_trivially_destructible_v<Saturated<uint64_t>>);
static_assert(std::is_standard_layout_v<Saturated<uint64_t>>);

static_assert(!std::is_same_v<Saturated<uint64_t>, uint64_t>);
static_assert(std::is_convertible_v<uint64_t, Saturated<uint64_t>>);
static_assert(!std::is_convertible_v<Saturated<uint64_t>, uint64_t>);

namespace detail::saturated_self_test {

using Sat64 = Saturated<uint64_t>;
using SatI32 = Saturated<int32_t>;

[[nodiscard]] consteval bool default_zeroes() noexcept {
    Sat64 s{};
    return s.value() == 0 && !s.was_clamped();
}
static_assert(default_zeroes());

[[nodiscard]] consteval bool implicit_from_value() noexcept {
    Sat64 s = uint64_t{42};
    return s.value() == 42 && !s.was_clamped();
}
static_assert(implicit_from_value());

[[nodiscard]] consteval bool explicit_two_arg() noexcept {
    Sat64 s{uint64_t{99}, true};
    return s.value() == 99 && s.was_clamped();
}
static_assert(explicit_two_arg());

[[nodiscard]] consteval bool explicit_t_conversion() noexcept {
    Sat64 s{uint64_t{123}, true};
    auto v = static_cast<uint64_t>(s);
    return v == 123;
}
static_assert(explicit_t_conversion());

[[nodiscard]] consteval bool equality_pair_compare() noexcept {
    Sat64 a{uint64_t{5}, false};
    Sat64 b{uint64_t{5}, false};
    Sat64 c{uint64_t{5}, true};
    Sat64 d{uint64_t{6}, false};
    return (a == b) && !(a == c) && !(a == d);
}
static_assert(equality_pair_compare());

[[nodiscard]] consteval bool add_sat_no_overflow() noexcept {
    auto s = add_sat_checked<uint64_t>(100, 200);
    return s.value() == 300 && !s.was_clamped();
}
static_assert(add_sat_no_overflow());

[[nodiscard]] consteval bool add_sat_unsigned_overflow() noexcept {
    auto s = add_sat_checked<uint8_t>(200, 100);
    return s.value() == std::numeric_limits<uint8_t>::max() && s.was_clamped();
}
static_assert(add_sat_unsigned_overflow());

[[nodiscard]] consteval bool add_sat_signed_pos_overflow() noexcept {
    auto s = add_sat_checked<int8_t>(int8_t{100}, int8_t{50});
    return s.value() == std::numeric_limits<int8_t>::max() && s.was_clamped();
}
static_assert(add_sat_signed_pos_overflow());

[[nodiscard]] consteval bool add_sat_signed_neg_overflow() noexcept {
    auto s = add_sat_checked<int8_t>(int8_t{-100}, int8_t{-50});
    return s.value() == std::numeric_limits<int8_t>::min() && s.was_clamped();
}
static_assert(add_sat_signed_neg_overflow());

[[nodiscard]] consteval bool sub_sat_unsigned_underflow() noexcept {
    auto s = sub_sat_checked<uint64_t>(5, 10);
    return s.value() == 0 && s.was_clamped();
}
static_assert(sub_sat_unsigned_underflow());

[[nodiscard]] consteval bool sub_sat_signed_overflow() noexcept {
    auto s = sub_sat_checked<int8_t>(int8_t{100}, int8_t{-50});
    return s.value() == std::numeric_limits<int8_t>::max() && s.was_clamped();
}
static_assert(sub_sat_signed_overflow());

[[nodiscard]] consteval bool mul_sat_unsigned_overflow() noexcept {
    auto s = mul_sat_checked<uint8_t>(20, 20);
    return s.value() == std::numeric_limits<uint8_t>::max() && s.was_clamped();
}
static_assert(mul_sat_unsigned_overflow());

[[nodiscard]] consteval bool mul_sat_signed_pos_pos_overflow() noexcept {
    auto s = mul_sat_checked<int8_t>(int8_t{50}, int8_t{50});
    return s.value() == std::numeric_limits<int8_t>::max() && s.was_clamped();
}
static_assert(mul_sat_signed_pos_pos_overflow());

[[nodiscard]] consteval bool mul_sat_signed_neg_pos_overflow() noexcept {
    auto s = mul_sat_checked<int8_t>(int8_t{-50}, int8_t{50});
    return s.value() == std::numeric_limits<int8_t>::min() && s.was_clamped();
}
static_assert(mul_sat_signed_neg_pos_overflow());

[[nodiscard]] consteval bool mul_sat_no_overflow() noexcept {
    auto s = mul_sat_checked<uint64_t>(7, 6);
    return s.value() == 42 && !s.was_clamped();
}
static_assert(mul_sat_no_overflow());

static_assert(Sat64::wrapper_kind() == "structural::Saturated");

// A floating-point element is admitted by the arithmetic constraint, so
// the type-system properties are checked here.  The value cannot be,
// because comparing floating-point values with equality is a build
// error in this project.
static_assert(std::is_arithmetic_v<float>);
static_assert(sizeof(Saturated<float>) == 8);

inline void runtime_smoke_test() {
    Sat64 a{};
    if (a.value() != 0 || a.was_clamped()) std::abort();

    Sat64 b = uint64_t{777};
    if (b.value() != 777 || b.was_clamped()) std::abort();

    Sat64 c{uint64_t{888}, true};
    if (c.value() != 888 || !c.was_clamped()) std::abort();

    auto add_ok = add_sat_checked<uint64_t>(10, 20);
    if (add_ok.value() != 30 || add_ok.was_clamped()) std::abort();

    auto add_clamped = add_sat_checked<uint8_t>(uint8_t{200}, uint8_t{100});
    if (add_clamped.value() != std::numeric_limits<uint8_t>::max()) std::abort();
    if (!add_clamped.was_clamped()) std::abort();

    auto sub_clamped = sub_sat_checked<uint64_t>(5, 10);
    if (sub_clamped.value() != 0 || !sub_clamped.was_clamped()) std::abort();

    auto mul_clamped = mul_sat_checked<uint8_t>(uint8_t{20}, uint8_t{20});
    if (mul_clamped.value() != std::numeric_limits<uint8_t>::max()) std::abort();
    if (!mul_clamped.was_clamped()) std::abort();

    auto mul_ok = mul_sat_checked<uint64_t>(7, 6);
    if (mul_ok.value() != 42 || mul_ok.was_clamped()) std::abort();

    Sat64 eq_a{uint64_t{5}, false};
    Sat64 eq_b{uint64_t{5}, false};
    Sat64 eq_c{uint64_t{5}, true};
    if (!(eq_a == eq_b)) std::abort();
    if (eq_a == eq_c) std::abort();

    auto raw = static_cast<uint64_t>(eq_c);
    if (raw != 5) std::abort();

    Sat64 src{uint64_t{0xDEADBEEFCAFEBABEull}, true};
    Sat64 dst;
    dst = src;
    if (dst.value() != src.value() || dst.was_clamped() != src.was_clamped()) {
        std::abort();
    }
}

}  // namespace detail::saturated_self_test

}  // namespace crucible::safety
