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

// gcd is unsigned and total.
static_assert(std::is_same_v<decltype(gcd(1, 2)), unsigned>
              && std::is_same_v<decltype(gcd(std::uint8_t{1}, std::uint8_t{2})), std::uint8_t>);
static_assert(gcd(0, 0) == 0u && gcd(0, 12) == 12u && gcd(12, 0) == 12u && gcd(12, 18) == 6u);
static_assert(gcd(-12, 18) == 6u && gcd(-12, -18) == 6u && gcd(17, 5) == 1u);
static_assert(gcd(min_of<std::int64_t>, std::int64_t{0}) == std::uint64_t{1} << 63);
static_assert(gcd(min_of<std::int64_t>, min_of<std::int64_t>) == std::uint64_t{1} << 63);
static_assert(gcd(std::uint8_t{128}, std::uint8_t{192}) == std::uint8_t{64});

// widening_mul gives the low half first.
static_assert(widening_mul(max_of<std::uint64_t>, max_of<std::uint64_t>).low == 1);
static_assert(widening_mul(max_of<std::uint64_t>, max_of<std::uint64_t>).high == max_of < std::uint64_t > -1);
static_assert(widening_mul(std::uint32_t{0x80000000u}, std::uint32_t{4}).high == 2);
static_assert(widening_mul(std::uint32_t{0x80000000u}, std::uint32_t{4}).low == 0);
static_assert(widening_mul(std::uint8_t{255}, std::uint8_t{255}).high == 254);
static_assert(widening_mul(std::uint8_t{255}, std::uint8_t{255}).low == 1);

// The layout: an Option of a value of four bytes has eight bytes, and a
// WideProduct of two words holds the low word first.
static_assert(sizeof(Option<std::uint32_t>) == 8 && sizeof(Option<std::uint64_t>) == 16);
static_assert(sizeof(WideProduct<std::uint64_t>) == 16);
static_assert(std::meta::offset_of(^^WideProduct<std::uint64_t>::low).bytes == 0);
static_assert(std::meta::offset_of(^^WideProduct<std::uint64_t>::high).bytes == 8);
static_assert(std::is_trivially_copyable_v<WideProduct<std::uint64_t>>);

}  // namespace foundation::core::detail::arithmetic_checks
