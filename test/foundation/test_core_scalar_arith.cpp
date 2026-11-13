// Tests of foundation/core/Arithmetic.h at run time.  Each operation is
// compared with a model in a 128-bit integer: over each pair of 8-bit
// values, over each pair of the edge values of each width, and over Philox
// pairs of each width.  The checked form has a value exactly when the exact
// result fits, the wrapping form is the exact result modulo 2^bits, and the
// saturating form is the exact result clamped to the limits.

#include <foundation/core/Arithmetic.h>

#include <foundation/Platform.h>

#include "philox_stream.h"

#include <cstdint>
#include <type_traits>

namespace {

namespace core = ::foundation::core;
using core::Option;

__extension__ using Signed128 = __int128;
__extension__ using Unsigned128 = unsigned __int128;

// The value, through a call that the optimizer cannot read, so each
// operation runs at run time.
template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

template <class T>
[[nodiscard]] bool holds(Option<T> result, T expected) noexcept {
    return result.is_some() && static_cast<Option<T>&&>(result).value_or(T{0}) == expected;
}

template <class T>
[[nodiscard]] constexpr bool fits(Signed128 exact) noexcept {
    return exact >= static_cast<Signed128>(core::min_of<T>) && exact <= static_cast<Signed128>(core::max_of<T>);
}

template <class T>
[[nodiscard]] constexpr T clamp_to(Signed128 exact) noexcept {
    if (exact < static_cast<Signed128>(core::min_of<T>)) return core::min_of<T>;
    if (exact > static_cast<Signed128>(core::max_of<T>)) return core::max_of<T>;
    return static_cast<T>(exact);
}

// The exact value modulo 2^bits, as a T.
template <class T>
[[nodiscard]] constexpr T modulo(Unsigned128 exact) noexcept {
    return static_cast<T>(static_cast<std::make_unsigned_t<T>>(exact));
}

// The exact sum, difference and product, and their wrapped values.  A
// product of two 64-bit values can need 128 bits, so its model is the
// product of the magnitudes with the sign apart.
template <class T>
struct Exact final {
    Signed128 value = 0;
    bool fits_model = true;
};

// The sign of a value.  An unsigned type is never negative, and the compare
// of an unsigned value with zero is a warning of -Wtype-limits.
template <class T>
[[nodiscard]] constexpr bool is_negative(T value) noexcept {
    if constexpr (std::is_signed_v<T>) {
        return value < T{0};
    } else {
        return false;
    }
}

// The magnitude of a value in 128 bits, for each value.
template <class T>
[[nodiscard]] constexpr Unsigned128 magnitude_of(T value) noexcept {
    Signed128 const wide = static_cast<Signed128>(value);
    return static_cast<Unsigned128>(wide < 0 ? -wide : wide);
}

template <class T>
[[nodiscard]] constexpr Exact<T> exact_product(T left, T right) noexcept {
    bool const negative = is_negative(left) != is_negative(right);
    Unsigned128 const magnitude = magnitude_of(left) * magnitude_of(right);
    if (magnitude > (Unsigned128{1} << 126)) return Exact<T>{0, false};
    Signed128 const signed_magnitude = static_cast<Signed128>(magnitude);
    return Exact<T>{negative ? -signed_magnitude : signed_magnitude, true};
}

// Each operation of the header on one pair, against the model.
template <class T>
void check_pair(T left, T right) {
    T const a = opaque(left);
    T const b = opaque(right);
    Signed128 const sum = static_cast<Signed128>(a) + static_cast<Signed128>(b);
    Signed128 const difference = static_cast<Signed128>(a) - static_cast<Signed128>(b);
    Exact<T> const product = exact_product(a, b);

    CRUCIBLE_FATAL_INVARIANT(core::checked_add(a, b).is_some() == fits<T>(sum));
    CRUCIBLE_FATAL_INVARIANT(!fits<T>(sum) || holds(core::checked_add(a, b), static_cast<T>(sum)));
    CRUCIBLE_FATAL_INVARIANT(core::wrapping_add(a, b) == modulo<T>(static_cast<Unsigned128>(sum)));
    CRUCIBLE_FATAL_INVARIANT(core::saturating_add(a, b) == clamp_to<T>(sum));

    CRUCIBLE_FATAL_INVARIANT(core::checked_sub(a, b).is_some() == fits<T>(difference));
    CRUCIBLE_FATAL_INVARIANT(!fits<T>(difference) || holds(core::checked_sub(a, b), static_cast<T>(difference)));
    CRUCIBLE_FATAL_INVARIANT(core::wrapping_sub(a, b) == modulo<T>(static_cast<Unsigned128>(difference)));
    CRUCIBLE_FATAL_INVARIANT(core::saturating_sub(a, b) == clamp_to<T>(difference));

    bool const product_fits = product.fits_model && fits<T>(product.value);
    CRUCIBLE_FATAL_INVARIANT(core::checked_mul(a, b).is_some() == product_fits);
    CRUCIBLE_FATAL_INVARIANT(!product_fits || holds(core::checked_mul(a, b), static_cast<T>(product.value)));
    Unsigned128 const wide_product =
        static_cast<Unsigned128>(static_cast<Signed128>(a)) * static_cast<Unsigned128>(static_cast<Signed128>(b));
    CRUCIBLE_FATAL_INVARIANT(core::wrapping_mul(a, b) == modulo<T>(wide_product));
    T const saturated = product.fits_model ? clamp_to<T>(product.value)
                                           : (is_negative(a) != is_negative(b) ? core::min_of<T> : core::max_of<T>);
    CRUCIBLE_FATAL_INVARIANT(core::saturating_mul(a, b) == saturated);

    if (b == T{0}) {
        CRUCIBLE_FATAL_INVARIANT(core::checked_div(a, b).is_none() && core::checked_rem(a, b).is_none());
    } else {
        Signed128 const quotient = static_cast<Signed128>(a) / static_cast<Signed128>(b);
        Signed128 const remainder = static_cast<Signed128>(a) % static_cast<Signed128>(b);
        CRUCIBLE_FATAL_INVARIANT(core::checked_div(a, b).is_some() == fits<T>(quotient));
        CRUCIBLE_FATAL_INVARIANT(!fits<T>(quotient) || holds(core::checked_div(a, b), static_cast<T>(quotient)));
        CRUCIBLE_FATAL_INVARIANT(holds(core::checked_rem(a, b), static_cast<T>(remainder)));
    }

    // gcd divides the two operands, and no larger value divides the two.
    Unsigned128 const divisor = core::gcd(a, b);
    Unsigned128 const left_magnitude = magnitude_of(a);
    Unsigned128 const right_magnitude = magnitude_of(b);
    if (divisor == 0) {
        CRUCIBLE_FATAL_INVARIANT(left_magnitude == 0 && right_magnitude == 0);
    } else {
        CRUCIBLE_FATAL_INVARIANT(left_magnitude % divisor == 0 && right_magnitude % divisor == 0);
        Unsigned128 const left_rest = left_magnitude / divisor;
        Unsigned128 const right_rest = right_magnitude / divisor;
        CRUCIBLE_FATAL_INVARIANT(
            core::gcd(opaque(static_cast<std::uint64_t>(left_rest)), static_cast<std::uint64_t>(right_rest)) == 1u
            || left_rest == 0 || right_rest == 0);
    }
}

// The negation and the shifts of one value, against the model.
template <class T>
void check_value(T input) {
    T const value = opaque(input);
    Signed128 const negation = -static_cast<Signed128>(value);
    CRUCIBLE_FATAL_INVARIANT(core::checked_neg(value).is_some() == fits<T>(negation));
    CRUCIBLE_FATAL_INVARIANT(!fits<T>(negation) || holds(core::checked_neg(value), static_cast<T>(negation)));
    CRUCIBLE_FATAL_INVARIANT(core::wrapping_neg(value) == modulo<T>(static_cast<Unsigned128>(negation)));
    for (int count = -2; count < core::bits_of<T> + 2; ++count) {
        bool const in_range = count >= 0 && count < core::bits_of<T>;
        int const step = static_cast<int>(static_cast<unsigned>(count) & static_cast<unsigned>(core::bits_of<T> - 1));
        Signed128 const scaled = in_range ? static_cast<Signed128>(value) * (Signed128{1} << count) : 0;
        CRUCIBLE_FATAL_INVARIANT(core::checked_shl(value, count).is_some() == (in_range && fits<T>(scaled)));
        CRUCIBLE_FATAL_INVARIANT(!(in_range && fits<T>(scaled))
                                 || holds(core::checked_shl(value, count), static_cast<T>(scaled)));
        CRUCIBLE_FATAL_INVARIANT(core::checked_shr(value, count).is_some() == in_range);
        // The right shift is the floor of the quotient by 2^n.
        Signed128 const power = Signed128{1} << step;
        Signed128 const floored = static_cast<Signed128>(value) >= 0
                                    ? static_cast<Signed128>(value) / power
                                    : -((-static_cast<Signed128>(value) + power - 1) / power);
        CRUCIBLE_FATAL_INVARIANT(!in_range || holds(core::checked_shr(value, count), static_cast<T>(floored)));
        CRUCIBLE_FATAL_INVARIANT(core::wrapping_shr(value, count) == static_cast<T>(floored));
        Unsigned128 const shifted = static_cast<Unsigned128>(static_cast<Signed128>(value)) << step;
        CRUCIBLE_FATAL_INVARIANT(core::wrapping_shl(value, count) == modulo<T>(shifted));
    }
}

// The values at the edges of a type: the limits, zero, one, minus one and
// their neighbours, and the powers of two near the middle.
template <class T>
[[nodiscard]] T edge_value(int position) noexcept {
    switch (position) {
        case 0:
            return core::min_of<T>;
        case 1:
            return static_cast<T>(core::min_of<T> + 1);
        case 2:
            return core::max_of<T>;
        case 3:
            return static_cast<T>(core::max_of<T> - 1);
        case 4:
            return T{0};
        case 5:
            return T{1};
        case 6:
            return static_cast<T>(-1);
        case 7:
            return static_cast<T>(T{1} << (core::bits_of<T> / 2));
        case 8:
            return static_cast<T>(core::max_of<T> / 2);
        default:
            return static_cast<T>(core::max_of<T> / 2 + 1);
    }
}

inline constexpr int edge_count = 10;

template <class T>
void check_edges() {
    for (int first = 0; first < edge_count; ++first) {
        check_value(edge_value<T>(first));
        for (int second = 0; second < edge_count; ++second) {
            check_pair(edge_value<T>(first), edge_value<T>(second));
        }
    }
}

template <class T>
void check_random(std::uint64_t seed, int count) {
    ::crucible::test::PhiloxStream stream{seed};
    for (int index = 0; index < count; ++index) {
        T const left = static_cast<T>(stream.next_wide());
        T const right = static_cast<T>(stream.next_wide());
        // A narrow right operand reaches the small divisors and the shifts.
        T const small = static_cast<T>(stream.below(64));
        check_pair(left, right);
        check_pair(left, small);
        check_value(left);
    }
}

void test_each_pair_of_bytes() {
    for (int left = 0; left < 256; ++left) {
        check_value(static_cast<std::uint8_t>(left));
        check_value(static_cast<std::int8_t>(left));
        for (int right = 0; right < 256; ++right) {
            check_pair(static_cast<std::uint8_t>(left), static_cast<std::uint8_t>(right));
            check_pair(static_cast<std::int8_t>(left), static_cast<std::int8_t>(right));
        }
    }
}

void test_edges_of_each_width() {
    check_edges<std::int16_t>();
    check_edges<std::uint16_t>();
    check_edges<std::int32_t>();
    check_edges<std::uint32_t>();
    check_edges<std::int64_t>();
    check_edges<std::uint64_t>();
    check_edges<long long>();
    check_edges<unsigned long long>();
}

void test_philox_pairs_of_each_width() {
    check_random<std::int16_t>(42, 20000);
    check_random<std::uint16_t>(43, 20000);
    check_random<std::int32_t>(44, 20000);
    check_random<std::uint32_t>(45, 20000);
    check_random<std::int64_t>(46, 20000);
    check_random<std::uint64_t>(47, 20000);
}

// The widening product of two words is the 128-bit product, with the low
// half first.
template <class T>
void check_widening(T left, T right) {
    Unsigned128 const product = static_cast<Unsigned128>(opaque(left)) * static_cast<Unsigned128>(opaque(right));
    core::WideProduct<T> const halves = core::widening_mul(opaque(left), opaque(right));
    CRUCIBLE_FATAL_INVARIANT(halves.low == static_cast<T>(product));
    CRUCIBLE_FATAL_INVARIANT(halves.high == static_cast<T>(product >> core::bits_of<T>));
}

void test_widening_mul() {
    for (int left = 0; left < 256; ++left) {
        for (int right = 0; right < 256; ++right) {
            check_widening(static_cast<std::uint8_t>(left), static_cast<std::uint8_t>(right));
        }
    }
    ::crucible::test::PhiloxStream stream{48};
    for (int index = 0; index < 100000; ++index) {
        std::uint64_t const left = stream.next_wide();
        std::uint64_t const right = stream.next_wide();
        check_widening(left, right);
        check_widening(static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(right));
        check_widening(static_cast<std::uint16_t>(left), static_cast<std::uint16_t>(right));
    }
    check_widening(core::max_of<std::uint64_t>, core::max_of<std::uint64_t>);
    check_widening(core::max_of<unsigned long long>, core::max_of<unsigned long long>);
}

// gcd agrees with the algorithm of Euclid, also at the minimum of a signed
// type.
template <class T>
[[nodiscard]] std::make_unsigned_t<T> euclid(T left, T right) noexcept {
    Unsigned128 first = magnitude_of(left);
    Unsigned128 second = magnitude_of(right);
    while (second != 0) {
        Unsigned128 const rest = first % second;
        first = second;
        second = rest;
    }
    return static_cast<std::make_unsigned_t<T>>(first);
}

void test_gcd_against_euclid() {
    ::crucible::test::PhiloxStream stream{49};
    for (int index = 0; index < 100000; ++index) {
        std::uint64_t const left = stream.next_wide();
        std::uint64_t const right = stream.next_wide() >> stream.below(64);
        CRUCIBLE_FATAL_INVARIANT(core::gcd(opaque(left), opaque(right)) == euclid(left, right));
        auto const signed_left = static_cast<std::int64_t>(left);
        auto const signed_right = static_cast<std::int64_t>(right);
        CRUCIBLE_FATAL_INVARIANT(core::gcd(opaque(signed_left), opaque(signed_right))
                                 == euclid(signed_left, signed_right));
    }
    CRUCIBLE_FATAL_INVARIANT(core::gcd(opaque(core::min_of<std::int64_t>), std::int64_t{0}) == std::uint64_t{1} << 63);
    CRUCIBLE_FATAL_INVARIANT(core::gcd(opaque(std::int32_t{0}), std::int32_t{0}) == 0u);
}

}  // namespace

int main() {
    test_each_pair_of_bytes();
    test_edges_of_each_width();
    test_philox_pairs_of_each_width();
    test_widening_mul();
    test_gcd_against_euclid();
    return 0;
}
