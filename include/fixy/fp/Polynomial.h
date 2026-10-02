#pragma once

// The transcendentals here are written out rather than taken from the
// math library. The math library is the obvious choice and it loses on
// reproducibility: IEEE 754 leaves the accuracy of log, sin and cos
// unspecified, and implementations disagree in the last few units in the
// last place, so one input pair yields different bytes on different
// platforms. Every operation below is +, -, *, a fused multiply-add,
// sqrt, an absolute value, a sign copy, a comparison, a conversion or a
// bit_cast. IEEE 754 requires the fused multiply-add and sqrt to be
// correctly rounded, so std::fma and std::sqrt are the two library calls
// that give the same bits everywhere.
//
// Every product that feeds a sum is one std::fma. The compiler may fuse
// a written a * b + c into one operation under -ffp-contract=on or
// -ffp-contract=fast, when the target has a fused instruction, and leave
// it as two roundings otherwise. The same source then gives different
// bits in two builds. An explicit fma rounds once under every flag, on
// every target, and in constant evaluation. No expression below has the
// shape a * b + c, so no flag can change a result.
//
// The arithmetic assumes the IEEE default rounding mode, round to nearest
// with ties to even. A different dynamic rounding mode changes the bits.
//
// Constants are hex float literals so every bit of every coefficient is
// unambiguous.
//
// Design notes:
//
//  1. Every function is constexpr.  C++26 makes std::sqrt usable in a
//     constant expression, so the pinned values are static assertions
//     rather than a function that computes them and discards the result.
//     They are in test/layer/checks/fixy/fp/Polynomial.cpp, and one
//     translation unit checks them.  The functions are still callable at
//     runtime.
//
//  2. The value cells of that check file are the checks of this header,
//     and test/fixy/test_fp_polynomial.cpp calls each function once at
//     runtime so the non-constant path is exercised too.
//
//  3. The two unreachable switch arms call fixy::unreachable(), which ends
//     the process in each build.  The quadrant is masked to two bits, so
//     the four arms are exhaustive, and a default value is a value no
//     input can produce.  A zero for an impossible quadrant turns a future
//     reduction bug into a silently wrong sine rather than a crash.
//
//  4. Every value pin compares bit patterns, never floats.  A float
//     equality compare is a build error in this tree, and the bit pattern
//     is the stronger claim anyway: it is what a content hash folds.
//
//  5. log_poly, reduce_quarter_pi, sin_poly and cos_poly state their
//     domains as preconditions.  A reduction that accepts every float
//     converts the nearest multiple of pi/2 to int32, which is undefined
//     behaviour past about 3.4e9 and for an infinity or a NaN.  The angle
//     domain is one turn, where the answer is accurate.

#include <fixy/Core.h>
#include <foundation/contracts/Pre.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace fixy::fp {

namespace detail {

// Evaluates coefficients[0] + point * (coefficients[1] + point * (...)) in
// Horner form, one std::fma for each step.  O(N) in the number of
// coefficients.
template <std::size_t N>
[[nodiscard]] constexpr float horner(float point, const std::array<float, N>& coefficients) noexcept {
    static_assert(N >= 2, "a Horner chain needs a constant term and at least one more coefficient");
    float sum = coefficients[N - 1];
    for (std::size_t k = N - 1; k-- > 0;) {
        sum = std::fma(point, sum, coefficients[k]);
    }
    return sum;
}

// Each series below alternates in sign.  The table holds the magnitudes,
// constant term first, and the caller evaluates it at the negated
// variable, which is exact.

// log(1 + u) = u * (1 - u/2 + u^2/3 - ...) through the seventh power of u.
inline constexpr std::array<float, 7> kLogSeries{
    1.0f,
    0x1.0p-1f,  // 1/2
    0x1.5555560p-2f,  // 1/3
    0x1.0p-2f,  // 1/4
    0x1.99999ap-3f,  // 1/5
    0x1.5555560p-3f,  // 1/6
    0x1.2492494p-3f,  // 1/7
};

// sin(x) = x * (1 - x^2/3! + x^4/5! - ...) through the ninth power of x.
inline constexpr std::array<float, 5> kSinSeries{
    1.0f,
    0x1.555556p-3f,  // 1/6
    0x1.111112p-7f,  // 1/120
    0x1.A01A02p-13f,  // 1/5040
    0x1.5D8A4Cp-19f,  // 1/362880
};

// cos(x) = 1 - x^2/2! + x^4/4! - ... through the eighth power of x.
inline constexpr std::array<float, 5> kCosSeries{
    1.0f,
    0x1.0p-1f,  // 1/2
    0x1.555556p-5f,  // 1/24
    0x1.6C16C2p-10f,  // 1/720
    0x1.A01A02p-16f,  // 1/40320
};

}  // namespace detail

// The domain of log_poly: a finite float above zero that is not
// subnormal.  The reduction reads the exponent field as the exponent and
// restores the implicit leading one of the significand.  A zero, a
// subnormal, an infinity and a NaN each break that reading, and the sign
// bit is masked away, so each would give a wrong value, not a trap.
[[nodiscard]] constexpr bool is_positive_normal(float value) noexcept { return std::isnormal(value) && value > 0.0f; }

// Splitting the argument into 2^e * m leaves m in [1, 2), and the second
// reduction to [sqrt(2)/2, sqrt(2)] is what keeps the series usable.
// Without it the offset u = m - 1 approaches 1 just below a power of
// two, where the seven terms are far from converged and the result can
// come out with the wrong sign. The square root downstream then takes a
// negative argument and returns NaN. Reduced, |u| stays below 0.415.
// Measured against double precision over every positive normal float,
// the seven terms are within 8.4e-5 of the true value.
[[nodiscard]] constexpr float log_poly(float value) noexcept {
    CRUCIBLE_PRE(is_positive_normal(value));

    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::int32_t biased_exponent = static_cast<std::int32_t>((bits >> 23) & 0xFFu);
    std::int32_t exponent = biased_exponent - 127;

    const std::uint32_t mantissa_bits = (bits & 0x007FFFFFu) | (127u << 23);
    float mantissa = std::bit_cast<float>(mantissa_bits);

    // Every step of this reduction is exact in IEEE 754, so the branch
    // is taken on the same inputs everywhere.
    const float sqrt2 = 0x1.6A09E6p+0f;
    if (mantissa > sqrt2) {
        mantissa *= 0.5f;
        exponent += 1;
    }
    const float offset = mantissa - 1.0f;
    const float log_mantissa = offset * detail::horner(-offset, detail::kLogSeries);

    const float ln2 = 0x1.62E430p-1f;  // log(2)
    return std::fma(static_cast<float>(exponent), ln2, log_mantissa);
}

// One turn, 2*pi as a float.  It is exactly four times the float pi/2 of
// the reduction, and it is the largest angle box_muller_polynomial makes.
inline constexpr float kOneTurn = 0x1.921FB6p+2f;

// The domain of reduce_quarter_pi, sin_poly and cos_poly: an angle within
// one turn of zero.  Every angle has an equivalent there, and a caller
// with a larger angle must reduce it with the precision it needs.
//
// The reduction subtracts a multiple of one float constant for pi/2,
// whose error is about 4.4e-8, so its error grows with the multiple.  The
// multiple is at most 4 within one turn.  Measured against double
// precision over every float in the domain, sin_poly and cos_poly are
// within 2.0e-7 of the true value.  Past one turn the error doubles with
// each binade, to 0.46 at 2^24.  Past 2^24 * pi/2 the float of the
// multiple rounds, and past about 3.4e9 the multiple overflows int32 and
// the conversion is undefined behaviour.  An infinity is outside the
// domain, and so is a NaN, because every comparison with a NaN is false.
[[nodiscard]] constexpr bool is_within_one_turn(float angle) noexcept { return std::fabs(angle) <= kOneTurn; }

struct ReduceResult {
    float reduced = 0.0f;
    std::int32_t quadrant = 0;
};

[[nodiscard]] constexpr ReduceResult reduce_quarter_pi(float angle) noexcept {
    CRUCIBLE_PRE(is_within_one_turn(angle));

    const float two_over_pi = 0x1.45F306p-1f;  // 2/pi
    const float pi_over_two = 0x1.921FB6p+0f;  // pi/2

    // The nearest multiple of pi/2 comes from a bias and a conversion,
    // not from nearbyint. One fma adds half a unit, with the sign of the
    // angle, to the exact product and rounds once. The conversion then
    // truncates toward zero in every rounding mode.
    const float biased_multiple = std::fma(angle, two_over_pi, std::copysign(0.5f, angle));
    const std::int32_t multiple = static_cast<std::int32_t>(biased_multiple);

    const float reduced = std::fma(-static_cast<float>(multiple), pi_over_two, angle);
    const std::int32_t quadrant = static_cast<std::int32_t>(static_cast<std::uint32_t>(multiple) & 0x3u);

    return ReduceResult{reduced, quadrant};
}

// The caller must pass an angle already reduced into [-pi/4, pi/4], up to
// the rounding of the reduction. The Taylor series for sin through the
// ninth power, in Horner form.
[[nodiscard]] constexpr float sin_in_quarter(float reduced) noexcept {
    const float reduced_squared = reduced * reduced;
    return reduced * detail::horner(-reduced_squared, detail::kSinSeries);
}

// The caller must pass an angle already reduced into [-pi/4, pi/4], up to
// the rounding of the reduction. The Taylor series for cos through the
// eighth power, in Horner form.
[[nodiscard]] constexpr float cos_in_quarter(float reduced) noexcept {
    const float reduced_squared = reduced * reduced;
    return detail::horner(-reduced_squared, detail::kCosSeries);
}

[[nodiscard]] constexpr float sin_poly(float angle) noexcept {
    CRUCIBLE_PRE(is_within_one_turn(angle));

    const ReduceResult reduction = reduce_quarter_pi(angle);
    const float sine = sin_in_quarter(reduction.reduced);
    const float cosine = cos_in_quarter(reduction.reduced);
    // The quadrant is masked to two bits in reduce_quarter_pi, so these
    // four arms are the whole domain.
    switch (reduction.quadrant) {
        case 0:
            return sine;
        case 1:
            return cosine;
        case 2:
            return -sine;
        case 3:
            return -cosine;
        default:
            ::fixy::unreachable();
    }
}

[[nodiscard]] constexpr float cos_poly(float angle) noexcept {
    CRUCIBLE_PRE(is_within_one_turn(angle));

    const ReduceResult reduction = reduce_quarter_pi(angle);
    const float sine = sin_in_quarter(reduction.reduced);
    const float cosine = cos_in_quarter(reduction.reduced);
    switch (reduction.quadrant) {
        case 0:
            return cosine;
        case 1:
            return -sine;
        case 2:
            return -cosine;
        case 3:
            return sine;
        default:
            ::fixy::unreachable();
    }
}

[[nodiscard]] constexpr std::pair<float, float> box_muller_polynomial(std::uint32_t u1_raw,
                                                                      std::uint32_t u2_raw) noexcept {
    // The added one moves a raw word of zero off the bottom of the
    // range. The first uniform is then a positive normal float in
    // [2^-32, 1], which is the domain of log_poly.  The second uniform is
    // at most 1, so the angle is at most one turn.
    const float two_pow_neg32 = 0x1.0p-32f;
    const float first_uniform = (static_cast<float>(u1_raw) + 1.0f) * two_pow_neg32;
    const float second_uniform = (static_cast<float>(u2_raw) + 1.0f) * two_pow_neg32;

    const float minus_two_log = -2.0f * log_poly(first_uniform);
    const float radius = std::sqrt(minus_two_log);

    const float theta = kOneTurn * second_uniform;

    return {radius * cos_poly(theta), radius * sin_poly(theta)};
}

}  // namespace fixy::fp

namespace fixy::fp::detail {

// ── The pinned pair ─────────────────────────────────────────────────
//
// Computed in constant evaluation, and equal at run time on the
// development host with -ffp-contract=off and with -ffp-contract=on and
// FMA instructions, at -O0, -O1 and -O3.  Two words of Philox output in,
// one normal pair out.  A change to any coefficient, to the reduction, or
// to the order of the operations moves at least one of these words.

inline constexpr std::uint32_t kBoxMullerZ1 = 0x3E102B57U;  // 0x1.2056aep-3f
inline constexpr std::uint32_t kBoxMullerZ2 = 0xBF024D51U;  // -0x1.049aa2p-1f

}  // namespace fixy::fp::detail
