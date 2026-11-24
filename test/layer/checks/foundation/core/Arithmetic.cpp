// The compile-time checks of foundation/core/Arithmetic.h.

#include <foundation/core/Arithmetic.h>

#include <cstdint>
#include <meta>
#include <type_traits>

namespace foundation::core::detail::arithmetic_checks {

template <class T>
[[nodiscard]] consteval bool holds(Option<T> result, T expected) {
    return result.is_some() && static_cast<Option<T>&&>(result).value_or(T{0}) == expected;
}

template <class T>
[[nodiscard]] consteval bool empty(Option<T> result) {
    return result.is_none();
}

// The checked forms give the exact result or no value.
static_assert(holds(checked_add(std::uint8_t{200}, std::uint8_t{55}), std::uint8_t{255}));
static_assert(empty(checked_add(std::uint8_t{200}, std::uint8_t{56})));
static_assert(holds(checked_add(std::int8_t{-100}, std::int8_t{-28}), std::int8_t{-128}));
static_assert(empty(checked_add(std::int8_t{-100}, std::int8_t{-29})));
static_assert(empty(checked_add(max_of<std::int64_t>, std::int64_t{1})));
static_assert(holds(checked_sub(std::uint32_t{5}, std::uint32_t{5}), std::uint32_t{0}));
static_assert(empty(checked_sub(std::uint32_t{5}, std::uint32_t{6})));
static_assert(empty(checked_sub(min_of<std::int32_t>, 1)));
static_assert(holds(checked_mul(std::uint16_t{255}, std::uint16_t{257}), std::uint16_t{65535}));
static_assert(empty(checked_mul(std::uint16_t{256}, std::uint16_t{256})));
static_assert(empty(checked_mul(min_of<std::int64_t>, std::int64_t{-1})));
static_assert(holds(checked_mul(min_of<std::int64_t>, std::int64_t{1}), min_of<std::int64_t>));

// A zero divisor and the minimum by -1.  The remainder has the sign of the
// dividend.
static_assert(empty(checked_div(7, 0)) && empty(checked_rem(7, 0)));
static_assert(empty(checked_div(min_of<int>, -1)) && holds(checked_rem(min_of<int>, -1), 0));
static_assert(empty(checked_div(min_of<std::int8_t>, std::int8_t{-1})));
static_assert(holds(checked_div(-7, 2), -3) && holds(checked_rem(-7, 3), -1) && holds(checked_rem(7, -3), 1));
static_assert(holds(checked_div(std::uint64_t{17}, std::uint64_t{5}), std::uint64_t{3}));

// The negation of an unsigned value exists only for 0.
static_assert(holds(checked_neg(5), -5) && empty(checked_neg(min_of<int>))
              && holds(checked_neg(max_of<int>), -max_of<int>));
static_assert(holds(checked_neg(std::uint32_t{0}), std::uint32_t{0}) && empty(checked_neg(std::uint32_t{1})));

// The absolute value has no value for the minimum.  test_core_scalar_arith
// holds the refusal of an unsigned type.
static_assert(holds(checked_abs(-7), 7) && holds(checked_abs(7), 7) && holds(checked_abs(0), 0));
static_assert(empty(checked_abs(min_of<std::int8_t>)) && holds(checked_abs(std::int8_t{-127}), std::int8_t{127}));
static_assert(empty(checked_abs(min_of<std::int64_t>)));

// A shift count outside [0, bits) has no value, and a left shift that loses
// a bit or changes the sign has no value.
static_assert(holds(checked_shl(1, 30), 1 << 30) && empty(checked_shl(1, 31)) && empty(checked_shl(1, 32)));
static_assert(holds(checked_shl(-1, 31), min_of<int>) && empty(checked_shl(1, -1)));
static_assert(holds(checked_shl(-3, 2), -12) && empty(checked_shl(std::uint8_t{0x81}, 1)));
static_assert(holds(checked_shl(std::uint8_t{0x7F}, 1), std::uint8_t{0xFE}));
static_assert(holds(checked_shl(std::uint64_t{1}, 63), std::uint64_t{1} << 63)
              && empty(checked_shl(std::uint64_t{1}, 64)));
static_assert(holds(checked_shr(-8, 1), -4) && holds(checked_shr(-7, 1), -4) && empty(checked_shr(1, 32)));
static_assert(empty(checked_shr(1, -1)) && holds(checked_shr(std::uint16_t{0x8000}, 15), std::uint16_t{1}));

// The wrapping forms are modular, and a shift count is modulo the width.
static_assert(wrapping_add(std::uint8_t{200}, std::uint8_t{100}) == std::uint8_t{44});
static_assert(wrapping_add(max_of<int>, 1) == min_of<int> && wrapping_sub(min_of<int>, 1) == max_of<int>);
static_assert(wrapping_mul(std::uint16_t{256}, std::uint16_t{256}) == 0 && wrapping_neg(min_of<int>) == min_of<int>);
static_assert(wrapping_neg(std::uint8_t{1}) == std::uint8_t{255} && wrapping_neg(std::uint8_t{0}) == 0);
static_assert(wrapping_shl(std::uint64_t{1}, 64) == 1 && wrapping_shl(std::uint64_t{1}, 65) == 2);
static_assert(wrapping_shl(1, -1) == min_of<int> && wrapping_shl(std::uint8_t{3}, 7) == std::uint8_t{0x80});
static_assert(wrapping_shr(-8, 33) == -4 && wrapping_shr(std::uint32_t{8}, 35) == 1);
static_assert(wrapping_shl(1, min_of<int>) == 1 && wrapping_shr(1, min_of<int>) == 1);

// The saturating forms clamp to the nearest limit.
static_assert(saturating_add(std::uint8_t{200}, std::uint8_t{100}) == 255);
static_assert(saturating_add(std::int8_t{100}, std::int8_t{100}) == 127);
static_assert(saturating_add(std::int8_t{-100}, std::int8_t{-100}) == -128);
static_assert(saturating_sub(std::uint8_t{5}, std::uint8_t{6}) == 0);
static_assert(saturating_sub(std::int8_t{100}, std::int8_t{-100}) == 127);
static_assert(saturating_sub(std::int8_t{-100}, std::int8_t{100}) == -128);
static_assert(saturating_mul(std::int8_t{-100}, std::int8_t{2}) == -128);
static_assert(saturating_mul(std::int8_t{-100}, std::int8_t{-2}) == 127);
static_assert(saturating_mul(std::uint64_t{1} << 40, std::uint64_t{1} << 40) == max_of<std::uint64_t>);
static_assert(saturating_add(3, 4) == 7 && saturating_sub(3, 4) == -1 && saturating_mul(3, 4) == 12);

// The quotient of the minimum by -1 clamps to the maximum.  A zero divisor
// ends the process through fatal(), so its call is no constant.
template <auto Dividend, auto Divisor>
concept DividesAsConstant = requires {
    typename std::integral_constant<decltype(Dividend), ::foundation::core::saturating_div(Dividend, Divisor)>;
};
static_assert(saturating_div(min_of<int>, -1) == max_of<int> && saturating_div(min_of<int>, 1) == min_of<int>);
static_assert(saturating_div(min_of<std::int8_t>, std::int8_t{-1}) == max_of<std::int8_t>);
static_assert(saturating_div(-7, 2) == -3 && saturating_div(7, -2) == -3);
static_assert(saturating_div(std::uint8_t{255}, std::uint8_t{5}) == std::uint8_t{51});
static_assert(DividesAsConstant<7, 2> && !DividesAsConstant<7, 0>
              && !DividesAsConstant<std::uint64_t{7}, std::uint64_t{0}>);

// div_ceil rounds toward positive infinity.  A zero divisor and the minimum
// by -1 end the process through fatal(), so their calls are no constants.
template <auto Dividend, auto Divisor>
concept DividesCeilAsConstant =
    requires { typename std::integral_constant<decltype(Dividend), ::foundation::core::div_ceil(Dividend, Divisor)>; };
static_assert(div_ceil(7, 2) == 4 && div_ceil(-7, 2) == -3 && div_ceil(7, -2) == -3 && div_ceil(-7, -2) == 4);
static_assert(div_ceil(8, 2) == 4 && div_ceil(0, 5) == 0 && div_ceil(-8, -2) == 4 && div_ceil(1, -2) == 0);
static_assert(div_ceil(max_of<std::uint64_t>, std::uint64_t{2}) == std::uint64_t{1} << 63);
static_assert(div_ceil(min_of<std::int8_t>, std::int8_t{1}) == min_of<std::int8_t>);
static_assert(div_ceil(min_of<std::int8_t>, std::int8_t{-2}) == std::int8_t{64});
static_assert(std::is_same_v<decltype(div_ceil(std::uint8_t{1}, std::uint8_t{1})), std::uint8_t>);
static_assert(DividesCeilAsConstant<7, 2> && !DividesCeilAsConstant<7, 0> && !DividesCeilAsConstant<min_of<int>, -1>
              && !DividesCeilAsConstant<std::uint8_t{0}, std::uint8_t{0}>);

// gcd is unsigned and total.
static_assert(std::is_same_v<decltype(gcd(1, 2)), unsigned>
              && std::is_same_v<decltype(gcd(std::uint8_t{1}, std::uint8_t{2})), std::uint8_t>);
static_assert(gcd(0, 0) == 0u && gcd(0, 12) == 12u && gcd(12, 0) == 12u && gcd(12, 18) == 6u);
static_assert(gcd(-12, 18) == 6u && gcd(-12, -18) == 6u && gcd(17, 5) == 1u);
static_assert(gcd(min_of<std::int64_t>, std::int64_t{0}) == std::uint64_t{1} << 63);
static_assert(gcd(min_of<std::int64_t>, min_of<std::int64_t>) == std::uint64_t{1} << 63);
static_assert(gcd(std::uint8_t{128}, std::uint8_t{192}) == std::uint8_t{64});

// midpoint is the floor of the exact mean, the same for the two orders, and
// total at 0 and at the limits of each width: the sum of the two maxima does
// not wrap.
static_assert(std::is_same_v<decltype(midpoint(std::uint8_t{1}, std::uint8_t{2})), std::uint8_t>);
static_assert(midpoint(std::uint64_t{0}, std::uint64_t{0}) == 0u && midpoint(std::uint64_t{0}, std::uint64_t{1}) == 0u);
static_assert(midpoint(max_of<std::uint64_t>, max_of<std::uint64_t>) == max_of<std::uint64_t>);
static_assert(midpoint(max_of<std::uint64_t>, std::uint64_t{0xFFFFFFFFFFFFFFFEu})
              == std::uint64_t{0xFFFFFFFFFFFFFFFEu});
static_assert(midpoint(std::uint64_t{0}, max_of<std::uint64_t>) == max_of<std::uint64_t> / 2);
static_assert(midpoint(max_of<std::uint64_t>, std::uint64_t{0}) == max_of<std::uint64_t> / 2);
static_assert(midpoint(max_of<std::uint32_t>, max_of<std::uint32_t>) == max_of<std::uint32_t>);
static_assert(midpoint(std::uint32_t{0}, max_of<std::uint32_t>) == max_of<std::uint32_t> / 2);
static_assert(midpoint(std::uint8_t{255}, std::uint8_t{254}) == std::uint8_t{254});
static_assert(midpoint(max_of<std::int64_t>, max_of<std::int64_t>) == max_of<std::int64_t>);
static_assert(midpoint(min_of<std::int64_t>, min_of<std::int64_t>) == min_of<std::int64_t>);
static_assert(midpoint(min_of<std::int64_t>, max_of<std::int64_t>) == -1);
static_assert(midpoint(max_of<std::int64_t>, min_of<std::int64_t>) == -1);
static_assert(midpoint(std::int64_t{-3}, std::int64_t{0}) == -2 && midpoint(std::int64_t{3}, std::int64_t{0}) == 1);
static_assert(midpoint(min_of<std::int8_t>, max_of<std::int8_t>) == std::int8_t{-1});
static_assert(midpoint(min_of<std::int32_t>, min_of<std::int32_t>) == min_of<std::int32_t>);
static_assert(midpoint(-7, -4) == -6 && midpoint(-4, -7) == -6 && midpoint(4, 7) == 5 && midpoint(7, 4) == 5);

// widening_mul gives the low half first.
static_assert(widening_mul(max_of<std::uint64_t>, max_of<std::uint64_t>).low == 1);
static_assert(widening_mul(max_of<std::uint64_t>, max_of<std::uint64_t>).high == max_of < std::uint64_t > -1);
static_assert(widening_mul(std::uint32_t{0x80000000u}, std::uint32_t{4}).high == 2);
static_assert(widening_mul(std::uint32_t{0x80000000u}, std::uint32_t{4}).low == 0);
static_assert(widening_mul(std::uint8_t{255}, std::uint8_t{255}).high == 254);
static_assert(widening_mul(std::uint8_t{255}, std::uint8_t{255}).low == 1);

// checked_mul_div divides the exact product, so a product past the type
// still gives the quotient that the type holds.  A zero divisor and a
// quotient past the type have no value.  test_core_scalar_arith holds the
// refusal of a signed type, and its death tests end mul_div_floor for a zero
// divisor and a quotient past the type.
static_assert(holds(checked_mul_div(max_of<std::uint64_t>, max_of<std::uint64_t>, max_of<std::uint64_t>),
                    max_of<std::uint64_t>));
static_assert(empty(checked_mul_div(max_of<std::uint64_t>, std::uint64_t{6}, std::uint64_t{4})));
static_assert(holds(checked_mul_div(max_of<std::uint64_t>, std::uint64_t{3}, std::uint64_t{4}),
                    max_of<std::uint64_t> / 4 * 3 + 2));
static_assert(empty(checked_mul_div(std::uint64_t{7}, std::uint64_t{3}, std::uint64_t{0})));
static_assert(holds(checked_mul_div(std::uint8_t{200}, std::uint8_t{200}, std::uint8_t{250}), std::uint8_t{160}));
static_assert(empty(checked_mul_div(std::uint8_t{200}, std::uint8_t{200}, std::uint8_t{156})));
static_assert(mul_div_floor(std::uint32_t{10}, std::uint32_t{10}, std::uint32_t{3}) == 33u);

// A strict form gives the exact result in a constant expression.  An
// overflow, a zero divisor and a count outside the width end the process,
// so their calls are no constants.
template <auto Left, auto Right>
concept AddsStrictlyAsConstant =
    requires { typename std::integral_constant<decltype(Left), ::foundation::core::strict_add(Left, Right)>; };
template <auto Dividend, auto Divisor>
concept DividesStrictlyAsConstant = requires {
    typename std::integral_constant<decltype(Dividend), ::foundation::core::strict_div(Dividend, Divisor)>;
};
template <auto Value, int Count>
concept ShiftsStrictlyAsConstant =
    requires { typename std::integral_constant<decltype(Value), ::foundation::core::strict_shl(Value, Count)>; };
static_assert(strict_add(std::uint8_t{200}, std::uint8_t{55}) == std::uint8_t{255});
static_assert(strict_add(std::int8_t{-100}, std::int8_t{-28}) == std::int8_t{-128});
static_assert(strict_sub(std::uint32_t{5}, std::uint32_t{5}) == 0u && strict_sub(-3, 4) == -7);
static_assert(strict_mul(std::uint16_t{255}, std::uint16_t{257}) == std::uint16_t{65535});
static_assert(strict_div(-7, 2) == -3 && strict_rem(-7, 3) == -1 && strict_rem(7, -3) == 1);
static_assert(strict_rem(min_of<int>, -1) == 0 && strict_div(min_of<int>, 1) == min_of<int>);
static_assert(strict_shl(1, 30) == 1 << 30 && strict_shl(-1, 31) == min_of<int> && strict_shl(-3, 2) == -12);
static_assert(strict_shl(std::uint64_t{1}, 63) == std::uint64_t{1} << 63 && strict_shr(-8, 1) == -4);
static_assert(strict_shr(std::uint16_t{0x8000}, 15) == std::uint16_t{1});
static_assert(AddsStrictlyAsConstant<std::uint8_t{200}, std::uint8_t{55}>
              && !AddsStrictlyAsConstant<std::uint8_t{200}, std::uint8_t{56}>
              && !AddsStrictlyAsConstant<max_of<std::int64_t>, std::int64_t{1}>);
static_assert(DividesStrictlyAsConstant<7, 2> && !DividesStrictlyAsConstant<7, 0>
              && !DividesStrictlyAsConstant<min_of<int>, -1>);
static_assert(ShiftsStrictlyAsConstant<1, 30> && !ShiftsStrictlyAsConstant<1, 31> && !ShiftsStrictlyAsConstant<1, 32>
              && !ShiftsStrictlyAsConstant<1, -1>);

// A Strict holds one T with its size.  It has no implicit conversion from or
// to T, it takes only a T, and its operators take two Strict of one type.
template <class Left, class Right>
concept Adds = requires(Left left, Right right) { left + right; };
static_assert(sizeof(Strict<std::uint64_t>) == 8 && sizeof(Strict<std::uint8_t>) == 1);
static_assert(std::is_trivially_copyable_v<Strict<std::uint64_t>>);
static_assert(std::is_constructible_v<Strict<std::uint64_t>, std::uint64_t>);
static_assert(!std::is_convertible_v<std::uint64_t, Strict<std::uint64_t>>);
static_assert(!std::is_convertible_v<Strict<std::uint64_t>, std::uint64_t>);
static_assert(!std::is_constructible_v<Strict<std::uint64_t>, std::uint32_t>
              && !std::is_constructible_v<Strict<std::uint64_t>, int>
              && !std::is_constructible_v<Strict<std::uint32_t>, bool>
              && !std::is_constructible_v<Strict<std::uint64_t>, Strict<std::uint32_t>>);
static_assert(Adds<Strict<std::uint32_t>, Strict<std::uint32_t>> && !Adds<Strict<std::uint32_t>, Strict<std::uint64_t>>
              && !Adds<Strict<std::uint64_t>, std::uint64_t> && !Adds<std::uint64_t, Strict<std::uint64_t>>);
static_assert((Strict<int>{3} * Strict<int>{4} - Strict<int>{2}).raw() == 10);
static_assert((Strict<std::uint8_t>{std::uint8_t{1}} << 7).raw() == std::uint8_t{0x80});
static_assert(Strict<std::uint32_t>{}.raw() == 0u && Strict<int>{-1} < Strict<int>{0});

// The layout: an Option of a value of four bytes has eight bytes, and a
// WideProduct of two words holds the low word first.
static_assert(sizeof(Option<std::uint32_t>) == 8 && sizeof(Option<std::uint64_t>) == 16);
static_assert(sizeof(WideProduct<std::uint64_t>) == 16);
static_assert(std::meta::offset_of(^^WideProduct<std::uint64_t>::low).bytes == 0);
static_assert(std::meta::offset_of(^^WideProduct<std::uint64_t>::high).bytes == 8);
static_assert(std::is_trivially_copyable_v<WideProduct<std::uint64_t>>);

}  // namespace foundation::core::detail::arithmetic_checks
