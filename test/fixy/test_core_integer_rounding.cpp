// Tests of div_ceil, align_up and ilog2 of the Scalar family at run time.
//
// Each result is compared with the property that defines it, in 128-bit
// integers.  The quotient c of div_ceil(a, b) is the least c with c * b >= a
// for a positive b, and with c * b <= a for a negative b.  align_up(v, a) is
// the least multiple of a that is not less than v, or no value when a is not
// a power of two or the multiple is past the maximum.  ilog2(v) is the k with
// 2^k <= v < 2^(k+1), or no value for 0.  The inputs are each pair of 8-bit
// values, the edge values of each width and Philox values.
//
// A zero divisor and the minimum by -1 of div_ceil end the process.  Each
// death runs in a child process through expect_dies of the Check family.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include "../foundation/philox_stream.h"

#include <cstdint>
#include <type_traits>

namespace {

using ::fixy::check::expect_dies;

__extension__ using Signed128 = __int128;

// The value, through a call that the optimizer cannot read, so each
// operation runs at run time.
template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

template <class T>
[[nodiscard]] bool holds(::fixy::Option<T> result, T expected) noexcept {
    return result.is_some() && static_cast<::fixy::Option<T>&&>(result).value_or(T{0}) == expected;
}

// True when quotient is the ceiling of dividend / divisor: the product of
// the quotient is on the far side of the dividend, and the product of the
// quotient minus 1 is not.
[[nodiscard]] bool is_ceiling(Signed128 dividend, Signed128 divisor, Signed128 quotient) noexcept {
    Signed128 const product = quotient * divisor;
    Signed128 const lower = (quotient - 1) * divisor;
    if (divisor > 0) return product >= dividend && lower < dividend;
    return product <= dividend && lower > dividend;
}

template <class T>
[[nodiscard]] bool is_minimum_by_minus_one(T dividend, T divisor) noexcept {
    if constexpr (std::is_signed_v<T>) {
        return dividend == ::fixy::min_of<T> && divisor == T{-1};
    } else {
        return false;
    }
}

template <class T>
void check_div_ceil(::fixy::TestRunnerCtx const& test, T dividend, T divisor) {
    if (divisor == T{0} || is_minimum_by_minus_one(dividend, divisor)) return;
    T const quotient = ::fixy::div_ceil(opaque(dividend), opaque(divisor));
    CRUCIBLE_TEST_ASSERT(test, is_ceiling(dividend, divisor, quotient));
}

// The least multiple of alignment that is not less than value, in 128 bits,
// or a value past the maximum of T when no multiple fits.
template <class T>
[[nodiscard]] Signed128 least_multiple(T value, T alignment) noexcept {
    Signed128 const step = alignment;
    Signed128 const quotient = (static_cast<Signed128>(value) + step - 1) / step;
    return quotient * step;
}

template <class T>
void check_align_up(::fixy::TestRunnerCtx const& test, T value, T alignment) {
    ::fixy::Option<T> const aligned = ::fixy::align_up(opaque(value), opaque(alignment));
    bool const is_power = alignment != T{0} && (alignment & static_cast<T>(alignment - T{1})) == T{0};
    if (!is_power) {
        CRUCIBLE_TEST_ASSERT(test, aligned.is_none());
        return;
    }
    Signed128 const multiple = least_multiple(value, alignment);
    if (multiple > static_cast<Signed128>(::fixy::max_of<T>)) {
        CRUCIBLE_TEST_ASSERT(test, aligned.is_none());
        return;
    }
    CRUCIBLE_TEST_ASSERT(test, holds(aligned, static_cast<T>(multiple)));
}

template <class T>
void check_ilog2(::fixy::TestRunnerCtx const& test, T value) {
    ::fixy::Option<int> logarithm = ::fixy::ilog2(opaque(value));
    if (value == T{0}) {
        CRUCIBLE_TEST_ASSERT(test, logarithm.is_none());
        return;
    }
    int const exponent = static_cast<::fixy::Option<int>&&>(logarithm).value_or(-1);
    CRUCIBLE_TEST_ASSERT(test, exponent >= 0 && exponent < ::fixy::bits_of<T>);
    Signed128 const power = Signed128{1} << exponent;
    CRUCIBLE_TEST_ASSERT(test, power <= static_cast<Signed128>(value) && static_cast<Signed128>(value) < power * 2);
}

// The values at the edges of a type: the limits, zero, one, minus one, the
// neighbours of each, and the powers of two near the middle.
template <class T>
[[nodiscard]] T edge_value(int position) noexcept {
    switch (position) {
        case 0:
            return ::fixy::min_of<T>;
        case 1:
            return static_cast<T>(::fixy::min_of<T> + 1);
        case 2:
            return ::fixy::max_of<T>;
        case 3:
            return static_cast<T>(::fixy::max_of<T> - 1);
        case 4:
            return T{0};
        case 5:
            return T{1};
        case 6:
            return static_cast<T>(-1);
        case 7:
            return static_cast<T>(T{2});
        case 8:
            return static_cast<T>(::fixy::max_of<T> / 2);
        default:
            return static_cast<T>(::fixy::max_of<T> / 2 + 1);
    }
}

inline constexpr int edge_count = 10;

template <class T>
void check_edges(::fixy::TestRunnerCtx const& test) {
    for (int first = 0; first < edge_count; ++first) {
        for (int second = 0; second < edge_count; ++second) {
            check_div_ceil(test, edge_value<T>(first), edge_value<T>(second));
            if constexpr (std::is_unsigned_v<T>) {
                check_align_up(test, edge_value<T>(first), edge_value<T>(second));
            }
        }
        if constexpr (std::is_unsigned_v<T>) {
            check_ilog2(test, edge_value<T>(first));
            for (int power = 0; power < ::fixy::bits_of<T>; ++power) {
                auto const alignment = static_cast<T>(T{1} << power);
                check_align_up(test, edge_value<T>(first), alignment);
                check_ilog2(test, alignment);
                check_ilog2(test, static_cast<T>(alignment - T{1}));
            }
        }
    }
}

template <class T>
void check_random(::fixy::TestRunnerCtx const& test, std::uint64_t seed) {
    ::crucible::test::PhiloxStream stream{seed};
    for (int index = 0; index < 20000; ++index) {
        // A shift of a draw reaches the small values and the small divisors.
        auto const dividend = static_cast<T>(stream.next_wide() >> stream.below(64));
        auto const divisor = static_cast<T>(stream.next_wide() >> stream.below(64));
        check_div_ceil(test, dividend, divisor);
        if constexpr (std::is_unsigned_v<T>) {
            auto const alignment = static_cast<T>(T{1} << stream.below(static_cast<std::uint64_t>(::fixy::bits_of<T>)));
            check_align_up(test, dividend, alignment);
            check_align_up(test, dividend, divisor);
            check_ilog2(test, dividend);
        }
    }
}

void test_each_pair_of_bytes(::fixy::TestRunnerCtx const& test) {
    for (int left = 0; left < 256; ++left) {
        check_ilog2(test, static_cast<std::uint8_t>(left));
        for (int right = 0; right < 256; ++right) {
            check_div_ceil(test, static_cast<std::uint8_t>(left), static_cast<std::uint8_t>(right));
            check_div_ceil(test, static_cast<std::int8_t>(left), static_cast<std::int8_t>(right));
            check_align_up(test, static_cast<std::uint8_t>(left), static_cast<std::uint8_t>(right));
        }
    }
    for (int value = 0; value < 65536; ++value) {
        check_ilog2(test, static_cast<std::uint16_t>(value));
    }
}

void test_edges_and_draws_of_each_width(::fixy::TestRunnerCtx const& test) {
    check_edges<std::int16_t>(test);
    check_edges<std::uint16_t>(test);
    check_edges<std::int32_t>(test);
    check_edges<std::uint32_t>(test);
    check_edges<std::int64_t>(test);
    check_edges<std::uint64_t>(test);
    check_random<std::int32_t>(test, 61);
    check_random<std::uint32_t>(test, 62);
    check_random<std::int64_t>(test, 63);
    check_random<std::uint64_t>(test, 64);
}

// The sum (a + b - 1) of the usual ceiling wraps near the maximum.  div_ceil
// and align_up compute no such sum.
void test_no_wrap_near_the_maximum(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, ::fixy::div_ceil(opaque(::fixy::max_of<std::uint64_t>), std::uint64_t{2})
                                   == (std::uint64_t{1} << 63));
    CRUCIBLE_TEST_ASSERT(test, ::fixy::div_ceil(opaque(::fixy::max_of<std::int64_t>), std::int64_t{2})
                                   == (std::int64_t{1} << 62));
    CRUCIBLE_TEST_ASSERT(test, ::fixy::div_ceil(opaque(::fixy::min_of<std::int64_t>), std::int64_t{1})
                                   == ::fixy::min_of<std::int64_t>);
    CRUCIBLE_TEST_ASSERT(test, ::fixy::align_up(opaque(::fixy::max_of<std::uint64_t>), std::uint64_t{2}).is_none());
    CRUCIBLE_TEST_ASSERT(test, holds(::fixy::align_up(opaque(::fixy::max_of<std::uint64_t>), std::uint64_t{1}),
                                     ::fixy::max_of<std::uint64_t>));
}

void divide_by_zero() noexcept { static_cast<void>(::fixy::div_ceil(opaque(7), 0)); }

void divide_unsigned_by_zero() noexcept {
    static_cast<void>(::fixy::div_ceil(opaque(std::uint64_t{0}), std::uint64_t{0}));
}

void divide_minimum_by_minus_one() noexcept {
    static_cast<void>(::fixy::div_ceil(opaque(::fixy::min_of<std::int64_t>), std::int64_t{-1}));
}

void divide_byte_minimum_by_minus_one() noexcept {
    static_cast<void>(::fixy::div_ceil(opaque(::fixy::min_of<std::int8_t>), std::int8_t{-1}));
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_each_pair_of_bytes(test);
    test_edges_and_draws_of_each_width(test);
    test_no_wrap_near_the_maximum(test);

    ::fixy::TextView const zero_divisor{"div_ceil got the divisor 0, which gives no quotient"};
    ::fixy::TextView const minimum_by_minus_one{
        "div_ceil got the minimum by -1, whose quotient the type does not hold"};
    expect_dies(test, divide_by_zero, zero_divisor);
    expect_dies(test, divide_unsigned_by_zero, zero_divisor);
    expect_dies(test, divide_minimum_by_minus_one, minimum_by_minus_one);
    expect_dies(test, divide_byte_minimum_by_minus_one, minimum_by_minus_one);
    ::fixy::check::pass(test,
                        "div_ceil, align_up and ilog2 hold their definitions, and each refusal ends the process\n");
    return 0;
}
