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
#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>

namespace crucible::fixy::fp {

// The caller must pass x > 0.
//
// Splitting x into 2^e * m leaves m in [1, 2), and the second reduction
// to [sqrt(2)/2, sqrt(2)] is what keeps the series usable. Without it u
// approaches 1 just below a power of two, where the seven terms are far
// from converged and the result can come out with the wrong sign. The
// square root downstream then takes a negative argument and returns
// NaN. Reduced, |u| stays below 0.415 and seven terms land within an
// ulp.
[[nodiscard]] inline float log_poly(float x) noexcept {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(x);
    const std::int32_t e_raw = static_cast<std::int32_t>((bits >> 23) & 0xFFu);
    std::int32_t e_unbiased = e_raw - 127;

    const std::uint32_t m_bits = (bits & 0x007F'FFFFu) | (127u << 23);
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

[[nodiscard]] inline ReduceResult reduce_quarter_pi(float x) noexcept {
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
[[nodiscard]] inline float sin_in_quarter(float x) noexcept {
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
[[nodiscard]] inline float cos_in_quarter(float x) noexcept {
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

[[nodiscard]] inline float sin_poly(float x) noexcept {
    const ReduceResult rr = reduce_quarter_pi(x);
    const float s = sin_in_quarter(rr.reduced);
    const float c = cos_in_quarter(rr.reduced);
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
            return 0.0f;
    }
}

[[nodiscard]] inline float cos_poly(float x) noexcept {
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
            return 0.0f;
    }
}

[[nodiscard]] inline std::pair<float, float> box_muller_polynomial(std::uint32_t u1_raw,
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

// The locals below are volatile so the calls are not folded at compile
// time.
inline void runtime_smoke_test() noexcept {
    [[maybe_unused]] volatile float log1 = log_poly(1.0f);
    [[maybe_unused]] volatile float log2 = log_poly(2.0f);
    [[maybe_unused]] volatile float log_e = log_poly(0x1.5BF0A8p+1f);  // e

    [[maybe_unused]] volatile float s0 = sin_poly(0.0f);
    [[maybe_unused]] volatile float c0 = cos_poly(0.0f);
    [[maybe_unused]] volatile float spi2 = sin_poly(0x1.921FB6p+0f);  // pi/2
    [[maybe_unused]] volatile float cpi = cos_poly(0x1.921FB6p+1f);  // pi

    const auto [z1, z2] = box_muller_polynomial(0xDEADBEEFu, 0xCAFEBABEu);
    [[maybe_unused]] volatile float z1_v = z1;
    [[maybe_unused]] volatile float z2_v = z2;
}

}  // namespace crucible::fixy::fp
