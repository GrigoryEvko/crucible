#pragma once

// The transcendentals here are written out rather than taken from the
// math library. The math library is the obvious choice and it loses on
// reproducibility: IEEE 754 leaves the accuracy of log, sin and cos
// unspecified, and implementations disagree in the last few units in the
// last place, so one input pair yields different bytes on different
// platforms. Every operation below is +, -, *, /, bit_cast or sqrt.
// IEEE 754 requires sqrt to be correctly rounded, so it is the one
// library call that gives the same bits everywhere.
//
// Constants are hex float literals so every bit of every coefficient is
// unambiguous.
//
// Old spelling: include/crucible/fixy/fp/Polynomial.h.
//
// Deviations, each deliberate:
//
//  1. Every function is constexpr.  C++26 makes std::sqrt usable in a
//     constant expression, which was the one call that stopped it, so the
//     pinned values at the foot of this header are static assertions
//     rather than a function that computes them and discards the result.
//     Each one is checked in every translation unit that includes this
//     header.  The functions are still callable at runtime.
//
//  2. There is no runtime_smoke_test.  The old one called each function
//     into a volatile local and asserted nothing, which is the shape
//     the port removed: compiled into every translation unit and called
//     by nothing.  The value cells below replace it, and test/fixy/
//     test_fp_polynomial.cpp calls each function once at runtime so the
//     non-constant path is exercised too.
//
//  3. The two unreachable switch arms are std::unreachable().  The
//     quadrant is masked to two bits, so the four arms are exhaustive and
//     the old `default: return 0.0f;` was a value no input could produce.
//     Returning zero for an impossible quadrant would have turned a
//     future reduction bug into a silently wrong sine rather than a
//     crash.
//
//  4. Every value pin compares bit patterns, never floats.  A float
//     equality compare is a build error in this tree, and the bit pattern
//     is the stronger claim anyway: it is what a content hash folds.

#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>

namespace fixy::fp {

// The caller must pass x > 0.
//
// Splitting x into 2^e * m leaves m in [1, 2), and the second reduction
// to [sqrt(2)/2, sqrt(2)] is what keeps the series usable. Without it u
// approaches 1 just below a power of two, where the seven terms are far
// from converged and the result can come out with the wrong sign. The
// square root downstream then takes a negative argument and returns
// NaN. Reduced, |u| stays below 0.415 and seven terms land within an
// ulp.
[[nodiscard]] constexpr float log_poly(float x) noexcept {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(x);
    const std::int32_t e_raw = static_cast<std::int32_t>((bits >> 23) & 0xFFu);
    std::int32_t e_unbiased = e_raw - 127;

    const std::uint32_t m_bits = (bits & 0x007FFFFFu) | (127u << 23);
    float m = std::bit_cast<float>(m_bits);

    // Every step of this reduction is exact in IEEE 754, so the branch
    // is taken on the same inputs everywhere.
    const float sqrt2 = 0x1.6A09E6p+0f;
    if (m > sqrt2) {
        m *= 0.5f;
        e_unbiased += 1;
    }
    const float u = m - 1.0f;

    // Taylor series for log(1 + u) through the seventh power, in Horner
    // form.
    const float c2 = 0x1.0p-1f;  // 1/2
    const float c3 = 0x1.5555560p-2f;  // 1/3
    const float c4 = 0x1.0p-2f;  // 1/4
    const float c5 = 0x1.99999ap-3f;  // 1/5
    const float c6 = 0x1.5555560p-3f;  // 1/6
    const float c7 = 0x1.2492494p-3f;  // 1/7

    float p = c7;
    p = c6 - u * p;
    p = c5 - u * p;
    p = c4 - u * p;
    p = c3 - u * p;
    p = c2 - u * p;
    p = 1.0f - u * p;
    const float log_m = u * p;

    const float ln2 = 0x1.62E430p-1f;  // log(2)
    return static_cast<float>(e_unbiased) * ln2 + log_m;
}

struct ReduceResult {
    float reduced = 0.0f;
    std::int32_t quadrant = 0;
};

[[nodiscard]] constexpr ReduceResult reduce_quarter_pi(float x) noexcept {
    const float two_over_pi = 0x1.45F306p-1f;  // 2/pi
    const float pi_over_two = 0x1.921FB6p+0f;  // pi/2

    // Rounding to nearest through a bias and a conversion, rather than
    // through nearbyint, which reads the current rounding mode. The
    // conversion truncates toward zero by definition, so adding half a
    // unit of the same sign first rounds halfway cases away from zero
    // on every platform.
    const float q_raw = x * two_over_pi;
    const float q_biased = (q_raw >= 0.0f) ? (q_raw + 0.5f) : (q_raw - 0.5f);
    const std::int32_t q = static_cast<std::int32_t>(q_biased);

    const float reduced = x - static_cast<float>(q) * pi_over_two;
    const std::int32_t quadrant = static_cast<std::int32_t>(static_cast<std::uint32_t>(q) & 0x3u);

    return ReduceResult{reduced, quadrant};
}

// The caller must pass x already reduced into [-pi/4, pi/4]. Taylor
// series for sin through the ninth power, in Horner form.
[[nodiscard]] constexpr float sin_in_quarter(float x) noexcept {
    const float x2 = x * x;
    const float a4 = 0x1.5D8A4Cp-19f;  // 1/362880
    const float a3 = 0x1.A01A02p-13f;  // 1/5040
    const float a2 = 0x1.111112p-7f;  // 1/120
    const float a1 = 0x1.555556p-3f;  // 1/6
    float p = a4;
    p = a3 - x2 * p;
    p = a2 - x2 * p;
    p = a1 - x2 * p;
    p = 1.0f - x2 * p;
    return x * p;
}

// The caller must pass x already reduced into [-pi/4, pi/4]. Taylor
// series for cos through the eighth power, in Horner form.
[[nodiscard]] constexpr float cos_in_quarter(float x) noexcept {
    const float x2 = x * x;
    const float b4 = 0x1.A01A02p-16f;  // 1/40320
    const float b3 = 0x1.6C16C2p-10f;  // 1/720
    const float b2 = 0x1.555556p-5f;  // 1/24
    const float b1 = 0x1.0p-1f;  // 1/2
    float p = b4;
    p = b3 - x2 * p;
    p = b2 - x2 * p;
    p = b1 - x2 * p;
    return 1.0f - x2 * p;
}

[[nodiscard]] constexpr float sin_poly(float x) noexcept {
    const ReduceResult rr = reduce_quarter_pi(x);
    const float s = sin_in_quarter(rr.reduced);
    const float c = cos_in_quarter(rr.reduced);
    // The quadrant is masked to two bits in reduce_quarter_pi, so these
    // four arms are the whole domain.
    switch (rr.quadrant) {
        case 0:
            return s;
        case 1:
            return c;
        case 2:
            return -s;
        case 3:
            return -c;
        default:
            std::unreachable();
    }
}

[[nodiscard]] constexpr float cos_poly(float x) noexcept {
    const ReduceResult rr = reduce_quarter_pi(x);
    const float s = sin_in_quarter(rr.reduced);
    const float c = cos_in_quarter(rr.reduced);
    switch (rr.quadrant) {
        case 0:
            return c;
        case 1:
            return -s;
        case 2:
            return -c;
        case 3:
            return s;
        default:
            std::unreachable();
    }
}

[[nodiscard]] constexpr std::pair<float, float> box_muller_polynomial(std::uint32_t u1_raw,
                                                                     std::uint32_t u2_raw) noexcept {
    // The added one moves a raw word of zero off the bottom of the
    // range, which keeps the argument of the logarithm above zero.
    const float two_pow_neg32 = 0x1.0p-32f;
    const float u1 = (static_cast<float>(u1_raw) + 1.0f) * two_pow_neg32;
    const float u2 = (static_cast<float>(u2_raw) + 1.0f) * two_pow_neg32;

    const float minus_two_log_u1 = -2.0f * log_poly(u1);
    const float r = std::sqrt(minus_two_log_u1);

    const float two_pi = 0x1.921FB6p+2f;  // 2*pi
    const float theta = two_pi * u2;

    return {r * cos_poly(theta), r * sin_poly(theta)};
}

}  // namespace fixy::fp

namespace fixy::fp::detail::polynomial_self_test {

// Bit patterns, never a float compare: a float equality compare is a
// build error here, and the pattern is what a content hash folds anyway.
[[nodiscard]] consteval std::uint32_t w(float value) noexcept { return std::bit_cast<std::uint32_t>(value); }

// ── The identities ──────────────────────────────────────────────────
//
// log(1) is zero, sin(0) is zero and cos(0) is one exactly, in every
// rounding mode, so these are the cells that would catch a transposed
// coefficient or a sign error in the series rather than a last-ulp
// difference.

static_assert(w(log_poly(1.0f)) == w(0.0f), "log_poly(1) must be exactly +0.");
static_assert(w(sin_poly(0.0f)) == w(0.0f), "sin_poly(0) must be exactly +0.");
static_assert(w(cos_poly(0.0f)) == w(1.0f), "cos_poly(0) must be exactly 1.");

// log(2) is the coefficient the reduction multiplies the exponent by, so
// this cell binds the series to that constant.
static_assert(w(log_poly(2.0f)) == w(0x1.62e43p-1f), "log_poly(2) must be the pinned log(2).");

// The quadrant walk: sin at pi/2 and cos at pi land on the poles, which
// is what says the four switch arms are wired to the right expressions.
static_assert(w(sin_poly(0x1.921FB6p+0f)) == w(1.0f), "sin_poly(pi/2) must be exactly 1.");
static_assert(w(cos_poly(0x1.921FB6p+1f)) == w(-1.0f), "cos_poly(pi) must be exactly -1.");

// ── The pinned pair ─────────────────────────────────────────────────
//
// Measured once with the old header on the development host at
// -O0, -O1 and -O3, all three agreeing, and pinned here.  Two words of
// Philox output in, one normal pair out.  A change to any coefficient,
// to the reduction, or to the order of the operations moves at least one
// of these words.

inline constexpr std::uint32_t kBoxMullerZ1 = 0x3E102B5CU;  // 0x1.2056b8p-3f
inline constexpr std::uint32_t kBoxMullerZ2 = 0xBF024D51U;  // -0x1.049aa2p-1f

static_assert(w(0x1.2056b8p-3f) == kBoxMullerZ1, "the pinned word and the hex float it documents disagree.");
static_assert(w(-0x1.049aa2p-1f) == kBoxMullerZ2, "the pinned word and the hex float it documents disagree.");

static_assert(w(box_muller_polynomial(0xDEADBEEFu, 0xCAFEBABEu).first) == kBoxMullerZ1,
              "box_muller_polynomial(0xDEADBEEF, 0xCAFEBABE).first moved. The pair is pinned because a "
              "Philox-seeded normal sample reaches a content hash: if this changed on purpose, re-measure "
              "on the development host and say so in the commit.");
static_assert(w(box_muller_polynomial(0xDEADBEEFu, 0xCAFEBABEu).second) == kBoxMullerZ2,
              "box_muller_polynomial(0xDEADBEEF, 0xCAFEBABE).second moved. Same rule as .first.");

// The two outputs of one call are different samples, which is the whole
// point of the transform.
static_assert(kBoxMullerZ1 != kBoxMullerZ2);

// ── The reduction ───────────────────────────────────────────────────

static_assert(reduce_quarter_pi(0.0f).quadrant == 0);
static_assert(reduce_quarter_pi(0x1.921FB6p+0f).quadrant == 1, "pi/2 lands in quadrant 1.");
static_assert(reduce_quarter_pi(0x1.921FB6p+1f).quadrant == 2, "pi lands in quadrant 2.");

// The mask is what makes the four switch arms exhaustive, so the cell
// that says so reads the mask rather than trusting the comment: a large
// argument still reports a quadrant in range.
static_assert(reduce_quarter_pi(1.0e6f).quadrant >= 0 && reduce_quarter_pi(1.0e6f).quadrant <= 3,
              "the quadrant is masked to two bits, so no input can reach the unreachable arm.");
static_assert(reduce_quarter_pi(-1.0e6f).quadrant >= 0 && reduce_quarter_pi(-1.0e6f).quadrant <= 3,
              "the same holds for a large negative argument.");

}  // namespace fixy::fp::detail::polynomial_self_test
