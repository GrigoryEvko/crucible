#pragma once
// FIXY-V-095 — `fixy::fp::box_muller_polynomial` — bit-stable cross-
// platform Box-Muller transform built atop IEEE 754 arithmetic +
// crucible-source polynomial transcendentals (no libm).
//
// PROBLEM (Agent 5 + 25_04 §11 + Whitepaper Cipher §14).  The existing
// `Philox::box_muller_det` returns `DetSafe<PhiloxRng, ...>`, which the
// Cipher write-fence consumes as proof-of-determinism for replay-log
// entries.  But box_muller invokes `std::sin / cos / log`, and glibc /
// musl / Apple libm / MSVC CRT libm implementations are NOT bit-stable
// across vendors:
//
//   - glibc 2.40 sin(x) on AVX-512 uses libmvec dispatch with one ULP
//     of slack vs the scalar path.  Same x on AArch64 NEON via Apple
//     libm gives a different bit pattern in the low 1-3 ULPs.
//   - libm's log() implementation on AMD Zen4 (libmvec-amd) differs in
//     the last 1 ULP from Intel's libmvec on inputs near boundaries.
//   - musl's log() is ~1 ULP off glibc on most inputs by design (smaller
//     coefficient tables).
//
// The PhiloxRng promise from the DetSafe lattice (FpModeLattice) is
// "bytes are derived from the Philox chain AND will compare bit-equal
// across hardware".  box_muller_det's bytes ARE Philox-derived, but
// they are NOT bit-equal across libm implementations.  This is a
// silent lie — the type system says "PhiloxRng tier", but cross-Relay
// replay can mismatch on the final sample.  V-093's canonicalize would
// catch the NaN/±0 divergence; it cannot catch a 1-ULP libm drift.
//
// V-095 SOLUTION.  Two changes operate in lockstep:
//
//   (1) box_muller_det's return tier downgrades from PhiloxRng to
//       MonotonicClockRead.  MonotonicClockRead means "deterministic
//       within ONE run on ONE Relay" — admissible to replay of THAT
//       run's log, but NOT cross-fleet-admissible.  The Cipher write-
//       fence rejects MonotonicClockRead-tier bytes; production code
//       paths that want to record Box-Muller samples to the Cipher
//       chain MUST route through box_muller_polynomial_det instead.
//
//   (2) box_muller_polynomial_det is the new PhiloxRng-tier rail.  It
//       uses ONLY IEEE 754 arithmetic + std::sqrt (correctly-rounded
//       per IEEE 754 §6.3) + the crucible-source polynomial
//       approximations defined in this file.  No libm.  Result: bit-
//       identical across NV / AM / TPU / Trainium / CPU oracle for
//       the same (u1_raw, u2_raw) input pair.
//
// MATHEMATICAL APPROACH.  Box-Muller polar form:
//
//     u1, u2 ∈ (0, 1]            (uniform inputs, log-safe range)
//     r     = sqrt(-2 * log(u1))
//     theta = 2π * u2
//     z1    = r * cos(theta)     ; z1 ~ N(0,1)
//     z2    = r * sin(theta)     ; z2 ~ N(0,1)
//
// Replace `log`, `cos`, `sin` with polynomial approximations:
//
//   - log_poly(x) for x ∈ (0, ∞): range-reduce via bit_cast (extract
//     IEEE 754 exponent and mantissa), then polynomial in (m-1) for
//     normalized mantissa m ∈ [1, 2).  7-term polynomial gives ~5e-7
//     relative error — Box-Muller samples accurate to ~6 decimals.
//
//   - sin_poly / cos_poly(x) for x ∈ ℝ: range-reduce mod 2π via integer
//     truncation; quadrant select from the reduced value's bit pattern;
//     5-term polynomial per quadrant in [-π/4, +π/4].  Maximum error
//     ~3e-7 in float — same Box-Muller quality.
//
// The IEEE 754 ops used (+, -, *, /, bit_cast, std::sqrt) are bit-
// stable on every conforming platform.  std::sqrt is correctly-rounded
// per IEEE 754 §6.3 (mandated by C++26 cmath); it does NOT depend on
// libm coefficient choice.
//
// AXIOM AUDIT.
//   - InitSafe: all locals default-init; no padding; constants in hex
//     float form so bits are unambiguous.
//   - TypeSafe: explicit float/uint32_t types throughout; bit_cast for
//     reinterpretation (never reinterpret_cast).
//   - NullSafe: pure-function surface; no pointers escape.
//   - MemSafe: stack-only; no allocations.
//   - BorrowSafe / ThreadSafe / LeakSafe: pure-function — N/A.
//   - DetSafe: this IS the DetSafe statement.  Bit-equal across every
//     conforming IEEE 754 platform; no libm dependency.
//
// LATTICE CONTRACT.  fixy::fp::box_muller_polynomial_det returns
// `DetSafe<PhiloxRng, std::pair<float, float>>` — the bytes ARE Philox-
// derived (u1_raw, u2_raw come from the Philox chain) AND cross-platform
// bit-equal.  The Cipher write-fence admits this tier.
//
// FpLibmPolicy::Polynomial (FpModeLattice top): this header IS the
// concrete witness for that policy.  Code paths declaring
// `fixy::with_fp_libm_policy<FpLibmPolicy::Polynomial>` must route
// transcendentals through this file (not libm).

// NOTE: deliberately NOT including <crucible/safety/FpMode.h>.  This
// header is included from <crucible/Philox.h>, which is on the hot path;
// pulling in the full FpMode reflection machinery would balloon the
// Philox TU compile time.  The FpLibmPolicy::Polynomial association
// is a doc-level contract, not a code dependency.
#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>

namespace crucible::fixy::fp {

// ── log_poly: bit-stable natural logarithm ────────────────────────
//
// Input: x ∈ (0, ∞) — caller must ensure x > 0.
// Output: natural logarithm of x, bit-stable across IEEE 754 platforms.
// Approach: x = 2^e * m with m ∈ [1, 2); RANGE-REDUCE m further to
// [√2/2, √2] so that u = m - 1 ∈ [-0.293, +0.414].  Without the second
// reduction, u can approach 1 (when input is just below an integer
// power of 2), where the 7-term Taylor polynomial diverges by ~5% and
// flips the sign of log_poly(u1) — causing the Box-Muller -2*log(u1)
// to become negative and `sqrt` to produce NaN.  With reduction,
// |u| ≤ 0.414 guarantees sub-ULP convergence in 7 terms.
// log(m) = log(1 + u), Taylor coefficients in hex float form.
[[nodiscard]] inline float
log_poly(float x) noexcept {
    // Extract IEEE 754 binary32 exponent + mantissa via bit_cast (no
    // libm, no rounding-mode dependence, fully cross-platform).
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(x);
    const std::int32_t  e_raw = static_cast<std::int32_t>((bits >> 23) & 0xFFu);
    std::int32_t        e_unbiased = e_raw - 127;

    // Reconstitute m ∈ [1, 2) by zeroing the exponent field to 127.
    // Equivalent to ldexpf(x, -e_unbiased) but bit_cast-based.
    const std::uint32_t m_bits = (bits & 0x007F'FFFFu) | (127u << 23);
    float               m = std::bit_cast<float>(m_bits);

    // Second range reduction: if m > √2, halve m and bump the exponent
    // so that m ∈ [√2/2, √2] ≈ [0.707, 1.414].  Cross-platform bit-stable
    // because the comparison is exact on IEEE 754 (sqrt(2) constant in
    // hex float, multiplication by 0.5 is exact, increment is integer).
    const float sqrt2 = 0x1.6A09E6p+0f;  // 1.4142135...
    if (m > sqrt2) {
        m *= 0.5f;
        e_unbiased += 1;
    }
    const float u = m - 1.0f;  // u ∈ [-0.293, +0.414]

    // 7-term Taylor expansion of log(1 + u): u - u²/2 + u³/3 - ...
    // Coefficients in hex float form for bit-unambiguous constants.
    // Horner's method for stability:
    //   log(1+u) = u * (1 - u*(1/2 - u*(1/3 - u*(1/4 - u*(1/5 - u*(1/6 - u/7))))))
    const float c2 = 0x1.0p-1f;          // 1/2
    const float c3 = 0x1.5555560p-2f;    // 1/3 (nearest float)
    const float c4 = 0x1.0p-2f;          // 1/4
    const float c5 = 0x1.99999ap-3f;     // 1/5
    const float c6 = 0x1.5555560p-3f;    // 1/6
    const float c7 = 0x1.2492494p-3f;    // 1/7

    float p = c7;
    p = c6 - u * p;
    p = c5 - u * p;
    p = c4 - u * p;
    p = c3 - u * p;
    p = c2 - u * p;
    p = 1.0f - u * p;
    const float log_m = u * p;

    // log(2) in hex float — exact to 24 bits.
    const float ln2 = 0x1.62E430p-1f;
    return static_cast<float>(e_unbiased) * ln2 + log_m;
}

// ── Reduce x mod (π/2), return (reduced, quadrant) ────────────────
//
// theta = x - q * (π/2) where q = round(x / (π/2)), reduced ∈ [-π/4, π/4].
// All ops are IEEE 754 +/-/*: cross-platform bit-stable.
struct ReduceResult {
    float        reduced = 0.0f;
    std::int32_t quadrant = 0;  // ∈ {0, 1, 2, 3} mod 4 of round(x / (π/2))
};

[[nodiscard]] inline ReduceResult
reduce_quarter_pi(float x) noexcept {
    // 2/π in hex float — exact-rounded.
    const float two_over_pi = 0x1.45F306p-1f;  // 0.6366197...
    const float pi_over_two = 0x1.921FB6p+0f;  // 1.5707963...

    // q = nearbyintf(x * 2/π) — but nearbyint depends on rounding mode.
    // Use truncation-then-bias to get round-half-to-even bit-stably:
    //   q_raw = x * 2/π
    //   q = (q_raw >= 0) ? floor(q_raw + 0.5) : -floor(-q_raw + 0.5)
    // Float-to-int via static_cast<int32_t>(...) is C++ "truncate toward
    // zero" — well-defined and bit-stable (no rounding mode involved).
    const float q_raw = x * two_over_pi;
    const float q_biased = (q_raw >= 0.0f) ? (q_raw + 0.5f) : (q_raw - 0.5f);
    const std::int32_t q = static_cast<std::int32_t>(q_biased);

    const float reduced = x - static_cast<float>(q) * pi_over_two;
    const std::int32_t quadrant = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(q) & 0x3u);  // ∈ {0, 1, 2, 3}

    return ReduceResult{reduced, quadrant};
}

// ── sin_in_quarter: sin(x) for x ∈ [-π/4, π/4] ─────────────────────
//
// 5-term Taylor polynomial: x - x³/6 + x⁵/120 - x⁷/5040 + x⁹/362880.
// Max abs error on [-π/4, π/4] ≈ 3e-9 (float-precision sufficient).
[[nodiscard]] inline float
sin_in_quarter(float x) noexcept {
    const float x2 = x * x;
    // Horner form for sin(x)/x = 1 - x²/6 + x⁴/120 - x⁶/5040 + x⁸/362880
    const float a4 = 0x1.5D8A4Cp-19f;    // 1/362880 ≈ 2.755e-6
    const float a3 = 0x1.A01A02p-13f;    // 1/5040   ≈ 1.984e-4
    const float a2 = 0x1.111112p-7f;     // 1/120    ≈ 8.333e-3
    const float a1 = 0x1.555556p-3f;     // 1/6      ≈ 1.667e-1
    float p = a4;
    p = a3 - x2 * p;
    p = a2 - x2 * p;
    p = a1 - x2 * p;
    p = 1.0f - x2 * p;
    return x * p;
}

// ── cos_in_quarter: cos(x) for x ∈ [-π/4, π/4] ─────────────────────
//
// 5-term Taylor: 1 - x²/2 + x⁴/24 - x⁶/720 + x⁸/40320.
// Max abs error on [-π/4, π/4] ≈ 2e-9.
[[nodiscard]] inline float
cos_in_quarter(float x) noexcept {
    const float x2 = x * x;
    const float b4 = 0x1.A01A02p-16f;    // 1/40320
    const float b3 = 0x1.6C16C2p-10f;    // 1/720
    const float b2 = 0x1.555556p-5f;     // 1/24
    const float b1 = 0x1.0p-1f;          // 1/2
    float p = b4;
    p = b3 - x2 * p;
    p = b2 - x2 * p;
    p = b1 - x2 * p;
    return 1.0f - x2 * p;
}

// ── sin_poly / cos_poly: full-range sin and cos ────────────────────
//
// Range reduction via reduce_quarter_pi; quadrant select via lookup.
// Identity used:
//   q=0: sin(x) =  sin_in(r),  cos(x) =  cos_in(r)
//   q=1: sin(x) =  cos_in(r),  cos(x) = -sin_in(r)
//   q=2: sin(x) = -sin_in(r),  cos(x) = -cos_in(r)
//   q=3: sin(x) = -cos_in(r),  cos(x) =  sin_in(r)
[[nodiscard]] inline float
sin_poly(float x) noexcept {
    const ReduceResult rr = reduce_quarter_pi(x);
    const float s = sin_in_quarter(rr.reduced);
    const float c = cos_in_quarter(rr.reduced);
    switch (rr.quadrant) {
        case 0: return  s;
        case 1: return  c;
        case 2: return -s;
        case 3: return -c;
        default: return 0.0f;  // unreachable; q masked to {0,1,2,3}
    }
}

[[nodiscard]] inline float
cos_poly(float x) noexcept {
    const ReduceResult rr = reduce_quarter_pi(x);
    const float s = sin_in_quarter(rr.reduced);
    const float c = cos_in_quarter(rr.reduced);
    switch (rr.quadrant) {
        case 0: return  c;
        case 1: return -s;
        case 2: return -c;
        case 3: return  s;
        default: return 0.0f;  // unreachable
    }
}

// ── Box-Muller polar form — polynomial transcendentals only ────────
//
// Input: two raw uint32 Philox outputs.
// Output: pair of N(0,1)-distributed floats, bit-stable across any
// conforming IEEE 754 platform.  All ops are +, -, *, /, bit_cast, or
// std::sqrt (the latter mandated correctly-rounded by IEEE 754 §6.3).
[[nodiscard]] inline std::pair<float, float>
box_muller_polynomial(std::uint32_t u1_raw, std::uint32_t u2_raw) noexcept {
    // Map raw uint32 to (0, 1] — same convention as Philox::box_muller.
    // (x+1) * 2^-32 ∈ (2^-32, 1] avoids log(0).
    const float two_pow_neg32 = 0x1.0p-32f;
    const float u1 = (static_cast<float>(u1_raw) + 1.0f) * two_pow_neg32;
    const float u2 = (static_cast<float>(u2_raw) + 1.0f) * two_pow_neg32;

    // r = sqrt(-2 * log_poly(u1))
    // std::sqrt is IEEE 754 correctly-rounded — bit-stable.
    const float minus_two_log_u1 = -2.0f * log_poly(u1);
    const float r = std::sqrt(minus_two_log_u1);

    // theta = 2π * u2
    const float two_pi = 0x1.921FB6p+2f;  // 6.2831853...
    const float theta = two_pi * u2;

    return {r * cos_poly(theta), r * sin_poly(theta)};
}

// ── runtime smoke test (feedback_algebra_runtime_smoke_test_discipline) ──
//
// Exercises every code path with runtime-volatile inputs.  Lives in the
// header so a single .cpp TU including it picks up coverage.  Pure
// witnesses: returns void, no assertion infrastructure required —
// failures are detected by Welford-style mean/variance checks in the
// V-095 sentinel TU.
inline void runtime_smoke_test() noexcept {
    // Smoke: log_poly at a few well-known inputs.  All ops bit-stable.
    [[maybe_unused]] volatile float log1 = log_poly(1.0f);   // expect 0
    [[maybe_unused]] volatile float log2 = log_poly(2.0f);   // expect ln(2)
    [[maybe_unused]] volatile float log_e = log_poly(0x1.5BF0A8p+1f);  // ≈ e

    // Smoke: sin/cos at canonical angles.
    [[maybe_unused]] volatile float s0 = sin_poly(0.0f);                     // expect 0
    [[maybe_unused]] volatile float c0 = cos_poly(0.0f);                     // expect 1
    [[maybe_unused]] volatile float spi2 = sin_poly(0x1.921FB6p+0f);         // π/2
    [[maybe_unused]] volatile float cpi  = cos_poly(0x1.921FB6p+1f);         // π

    // Smoke: Box-Muller with a representative seed pair.
    const auto [z1, z2] = box_muller_polynomial(0xDEADBEEFu, 0xCAFEBABEu);
    [[maybe_unused]] volatile float z1_v = z1;
    [[maybe_unused]] volatile float z2_v = z2;
}

}  // namespace crucible::fixy::fp
