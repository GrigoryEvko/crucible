#pragma once

// The Scalar family: compare and select.
//
//   min(a, b), max(a, b)  the smaller and the larger of two values of one
//                         type, by value
//   Bounds<T>             a range [lo, hi] with lo <= hi and no NaN
//   Bounds<T>::of(lo, hi) the range, or no value when lo > hi or a bound is
//                         a NaN
//   bounds<Lo, Hi>()      the range of two constants.  Lo > Hi or a NaN does
//                         not compile
//   clamp(v, range)       v in the range, with no check
//   compare(a, b)         the exact order of two integers of each sign
//   fits<To>(v)           true when the integer type To holds the value v
//   unsigned_abs(v)       the magnitude of a signed value, unsigned
//   abs_diff(a, b)        the distance of two values, unsigned
//
// min and max are the rule of std::min and std::max with no reference:
// `b < a ? b : a` and `a < b ? b : a`.  A NaN on either side gives a, and
// +0 and -0 give a.  The compare is exact, so the two forms keep the IEEE
// result on each target.  One compare and one select, a cmov or a minsd.
//
// A Bounds proves the order of its bounds, so clamp has no branch for it.
// clamp of a NaN gives lo.  Bounds carries the annotation
// no_start_over_bytes, so decode, a bit copy and a lifetime start cannot
// make a Bounds with lo > hi from bytes.
//
// compare of a signed and an unsigned value tests the sign first, so -1 is
// less than each unsigned value.  The plain operators convert -1 to the
// highest unsigned value.  fits uses no compare when each value of From is
// a value of To, and at most two compares in each other case.
//
// unsigned_abs and abs_diff give an unsigned result, so the magnitude of
// the minimum of a signed type is 2^(bits-1) and no value is undefined.

#include <foundation/ByteSeal.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Number.h>

#include <compare>
#include <type_traits>

namespace foundation::core {

template <Arithmetic T>
[[nodiscard]] constexpr T min(T left, T right) noexcept {
    return right < left ? right : left;
}

template <Arithmetic T>
[[nodiscard]] constexpr T max(T left, T right) noexcept {
    return left < right ? right : left;
}

template <Arithmetic T>
class[[= ::foundation::lifetime::no_start_over_bytes{}]] Bounds final {
    T lo_;
    T hi_;

    constexpr Bounds(T lo, T hi) noexcept : lo_{lo}, hi_{hi} {}

public:
    // The range [lo, hi], or no value when lo > hi or a bound is a NaN: a
    // NaN is not <= each value.  The result is an Option<Bounds>.  The
    // class is complete only in the body, so the body deduces the type.
    [[nodiscard]] static constexpr auto of(T lo, T hi) noexcept {
        using Range = Option<Bounds>;
        if (!(lo <= hi)) return Range{none};
        return Range::some(Bounds{lo, hi});
    }

    [[nodiscard]] constexpr T lo() const noexcept { return lo_; }
    [[nodiscard]] constexpr T hi() const noexcept { return hi_; }
};

namespace detail {

// Two constants of one Arithmetic type in order.  A NaN is not <= each
// value, so a NaN bound is refused too.
template <auto Lo, auto Hi>
concept OrderedBounds = Arithmetic<decltype(Lo)> && std::same_as<decltype(Lo), decltype(Hi)> && (Lo <= Hi);

}  // namespace detail

template <auto Lo, auto Hi>
    requires detail::OrderedBounds<Lo, Hi>
[[nodiscard]] consteval Bounds<decltype(Lo)> bounds() noexcept {
    return Bounds<decltype(Lo)>::of(Lo, Hi).expect("the constraint of bounds proves that Lo <= Hi");
}

// The test of hi comes first.  A NaN then gives lo, so each result is in the
// range, where std::clamp gives the NaN back.
template <Arithmetic T>
[[nodiscard]] constexpr T clamp(T value, Bounds<T> range) noexcept {
    return max(range.lo(), min(value, range.hi()));
}

namespace detail {

// True when left is less than right, exactly.  The usual arithmetic
// conversions can change a value, such as -1 to the highest unsigned value,
// so each branch converts the two values to a common type that holds each
// value of the two.  For a pair of two signs, the sign test comes first.
template <Integer Left, Integer Right>
[[nodiscard]] constexpr bool is_less(Left left, Right right) noexcept {
    if constexpr (std::is_signed_v<Left> == std::is_signed_v<Right>) {
        using Common = std::common_type_t<Left, Right>;
        return static_cast<Common>(left) < static_cast<Common>(right);
    } else if constexpr (std::is_signed_v<Left>) {
        using Common = std::common_type_t<std::make_unsigned_t<Left>, Right>;
        return left < Left{0} || static_cast<Common>(left) < static_cast<Common>(right);
    } else {
        using Common = std::common_type_t<Left, std::make_unsigned_t<Right>>;
        return right >= Right{0} && static_cast<Common>(left) < static_cast<Common>(right);
    }
}

}  // namespace detail

// The order tests less first.  Each other result is not less, so
// `std::is_lt(compare(a, b))` is the one test of std::cmp_less, and the
// optimizer removes the test of equality.
template <Integer Left, Integer Right>
[[nodiscard]] constexpr std::strong_ordering compare(Left left, Right right) noexcept {
    if (detail::is_less(left, right)) return std::strong_ordering::less;
    return detail::is_less(right, left) ? std::strong_ordering::greater : std::strong_ordering::equal;
}

template <Integer To, Integer From>
[[nodiscard]] constexpr bool fits(From value) noexcept {
    if constexpr (fits_each_value<From, To>) {
        return true;
    } else {
        return !detail::is_less(value, min_of<To>) && !detail::is_less(max_of<To>, value);
    }
}

template <SignedInteger T>
[[nodiscard]] constexpr std::make_unsigned_t<T> unsigned_abs(T value) noexcept {
    return detail::magnitude_of(value);
}

template <Integer T>
[[nodiscard]] constexpr std::make_unsigned_t<T> abs_diff(T left, T right) noexcept {
    using Bits = std::make_unsigned_t<T>;
    return left < right ? static_cast<Bits>(static_cast<Bits>(right) - static_cast<Bits>(left))
                        : static_cast<Bits>(static_cast<Bits>(left) - static_cast<Bits>(right));
}

}  // namespace foundation::core
