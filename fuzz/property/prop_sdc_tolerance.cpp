// ═══════════════════════════════════════════════════════════════════
// prop_sdc_tolerance.cpp — comparison-primitive fuzzer for the silent-
// data-corruption detector (observe/SdcDetect.h, detail:: namespace).
//
// SdcDetector compares redundant cross-Cog compute results to catch
// silent data corruption.  The comparison core is detail::tolerance_equal
// <T> / bitwise_equal<T> / equivalent<T> — templated over every result
// type, with FIVE distinct regimes: signed integral (overflow-safe
// modular abs-diff), unsigned integral, integral-promotion (int8/16),
// floating-point (fabs + uint64→T tolerance cast), and the bitwise
// memcmp fallback.  A false negative here is an UNDETECTED corruption;
// a false positive is a spurious Cog quarantine — so the comparison
// must be exactly right across the full input domain.
//
// test_observe_sdc_detect.cpp pins the single most important corner —
// signed tolerance must use TRUE distance, not modular distance
// (test_signed_tolerance_does_not_use_modular_distance) — and a small-
// delta case, both THROUGH the SdcDetector class wrapper with a handful
// of hand-picked values.  It never drives the detail comparison directly
// across the domain, never exercises float NaN/inf/±0, never checks
// symmetry or tolerance-monotonicity, and never covers the int8/16
// integer-promotion path or unsigned types.  This fuzzer closes that.
//
// ── Integer runs (8 widths) ──
// The oracle is an INDEPENDENT 128-bit absolute difference — a DIFFERENT
// computation from tolerance_equal's modular `(U)a - (U)b` trick (no
// wraparound; the true magnitude in __int128 space), so a divergence is
// a genuine bug.  Per (a, b, tolerance), corner-biased to {0, ±1, MIN,
// MAX} plus log-uniform random, it asserts:
//   * tolerance_equal == (wide_abs_diff(a,b) <= tolerance)   (oracle)
//   * symmetry: tolerance_equal(a,b,t) == tolerance_equal(b,a,t)
//   * reflexivity: tolerance_equal(a,a,t) for every t (delta 0 ≤ t)
//   * bitwise_equal(a,b) == (a == b)         (integral: bytes ⟺ value)
//   * equivalent() dispatches to the strategy's primitive
//
// ── Float runs (float, double) ──
// No independent oracle (a double reference would diverge from the
// float computation near the boundary), so oracle-free invariants with
// teeth — values drawn via bit_cast for full NaN/inf/denormal coverage:
//   * symmetry (fabs is sign-symmetric, holds even for NaN operands)
//   * reflexivity for FINITE a (NaN−NaN and inf−inf are NaN → not equal,
//     so reflexivity is finite-only — pinned explicitly)
//   * NaN operand ⇒ never tolerance-equal
//   * tolerance-monotonicity: equal at t ⇒ equal at UINT64_MAX, and
//     equal at 0 ⇒ equal at t (uint64→T cast is monotone non-decreasing)
//   * equivalent() dispatch
//
// All invariants verified clean by hand-trace before shipping — the
// comparison is correct; this is the full-domain regression net the
// two wrapper-level cases lack.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/observe/SdcDetect.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

namespace det = crucible::observe::detail;
using crucible::observe::SdcComparisonStrategy;
using crucible::fuzz::prop::Rng;

__extension__ using wide_u = unsigned __int128;
__extension__ using wide_s = __int128;

template <typename T>
struct IntSpec {
    T a = 0;
    T b = 0;
    std::uint64_t tolerance = 0;
};

template <typename T>
struct FloatSpec {
    T a = T{0};
    T b = T{0};
    std::uint64_t tolerance = 0;
};

template <typename T>
[[nodiscard]] T gen_int(Rng& rng) noexcept {
    switch (rng.next_below(8u)) {
        case 0: return T{0};
        case 1: return T{1};
        case 2: return std::numeric_limits<T>::max();
        case 3: return std::numeric_limits<T>::min();
        case 4: return static_cast<T>(~T{0});  // all-ones: -1 signed / max unsigned
        default: return static_cast<T>(rng.next64());
    }
}

[[nodiscard]] std::uint64_t gen_tolerance(Rng& rng) noexcept {
    switch (rng.next_below(6u)) {
        case 0: return 0u;
        case 1: return 1u;
        case 2: return std::numeric_limits<std::uint64_t>::max();
        case 3: return rng.next_below(16u);
        default: {
            const std::uint32_t bits = rng.next_below(64u);
            return bits == 0u ? 0u : (rng.next64() >> (64u - bits));
        }
    }
}

// Independent wide absolute difference: exact in 128-bit, no modular
// wraparound — a different computation from tolerance_equal's `(U)a-(U)b`.
template <typename T>
[[nodiscard]] wide_u wide_abs_diff(T a, T b) noexcept {
    if constexpr (std::signed_integral<T>) {
        const wide_s wa = a;
        const wide_s wb = b;
        const wide_s diff = wa >= wb ? wa - wb : wb - wa;
        return static_cast<wide_u>(diff);
    } else {
        const wide_u wa = a;
        const wide_u wb = b;
        return wa >= wb ? wa - wb : wb - wa;
    }
}

template <typename T>
[[nodiscard]] T gen_float(Rng& rng) noexcept {
    switch (rng.next_below(12u)) {
        case 0: return T{0};
        case 1: return -T{0};
        case 2: return T{1};
        case 3: return T{-1};
        case 4: return std::numeric_limits<T>::infinity();
        case 5: return -std::numeric_limits<T>::infinity();
        case 6: return std::numeric_limits<T>::quiet_NaN();
        case 7: return std::numeric_limits<T>::max();
        case 8: return std::numeric_limits<T>::lowest();
        case 9: return std::numeric_limits<T>::denorm_min();
        default:
            if constexpr (sizeof(T) == 4) {
                return std::bit_cast<T>(rng.next32());
            } else {
                return std::bit_cast<T>(rng.next64());
            }
    }
}

template <typename T>
[[nodiscard]] int run_int(const char* name, crucible::fuzz::prop::Config cfg) {
    return crucible::fuzz::prop::run(name, cfg,
        [](Rng& rng) noexcept -> IntSpec<T> {
            return IntSpec<T>{gen_int<T>(rng), gen_int<T>(rng), gen_tolerance(rng)};
        },
        [](const IntSpec<T>& spec) noexcept -> bool {
            const bool got = det::tolerance_equal<T>(spec.a, spec.b, spec.tolerance);
            const bool want = wide_abs_diff<T>(spec.a, spec.b) <=
                              static_cast<wide_u>(spec.tolerance);
            if (got != want) return false;
            if (det::tolerance_equal<T>(spec.b, spec.a, spec.tolerance) != got) return false;
            if (!det::tolerance_equal<T>(spec.a, spec.a, spec.tolerance)) return false;
            if (det::bitwise_equal<T>(spec.a, spec.b) != (spec.a == spec.b)) return false;
            if (det::equivalent<T>(spec.a, spec.b,
                    SdcComparisonStrategy::ArithmeticTolerance, spec.tolerance) != got) {
                return false;
            }
            if (det::equivalent<T>(spec.a, spec.b,
                    SdcComparisonStrategy::BitwiseEqual, spec.tolerance)
                != det::bitwise_equal<T>(spec.a, spec.b)) {
                return false;
            }
            return true;
        });
}

template <typename T>
[[nodiscard]] int run_float(const char* name, crucible::fuzz::prop::Config cfg) {
    return crucible::fuzz::prop::run(name, cfg,
        [](Rng& rng) noexcept -> FloatSpec<T> {
            return FloatSpec<T>{gen_float<T>(rng), gen_float<T>(rng), gen_tolerance(rng)};
        },
        [](const FloatSpec<T>& spec) noexcept -> bool {
            const T a = spec.a;
            const T b = spec.b;
            const std::uint64_t tol = spec.tolerance;
            const bool got = det::tolerance_equal<T>(a, b, tol);

            // Symmetry — fabs(a−b) == fabs(b−a) even when either is NaN.
            if (det::tolerance_equal<T>(b, a, tol) != got) return false;
            // Reflexivity is finite-only: NaN−NaN and inf−inf are NaN.
            if (std::isfinite(a) && !det::tolerance_equal<T>(a, a, tol)) return false;
            // A NaN operand can never be within tolerance.
            if ((std::isnan(a) || std::isnan(b)) && got) return false;
            // Tolerance-monotonicity: equal at t ⇒ equal at the max tol.
            if (got && !det::tolerance_equal<T>(a, b,
                    std::numeric_limits<std::uint64_t>::max())) {
                return false;
            }
            // equal at 0 ⇒ equal at any t (delta==0 ≤ every tolerance).
            if (det::tolerance_equal<T>(a, b, 0u) && !got) return false;
            // equivalent() dispatch.
            if (det::equivalent<T>(a, b, SdcComparisonStrategy::ArithmeticTolerance, tol)
                != got) {
                return false;
            }
            if (det::equivalent<T>(a, b, SdcComparisonStrategy::BitwiseEqual, tol)
                != det::bitwise_equal<T>(a, b)) {
                return false;
            }
            return true;
        });
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    int rc = 0;
    rc |= run_int<std::int8_t>("sdc_tol_i8", cfg);
    rc |= run_int<std::int16_t>("sdc_tol_i16", cfg);
    rc |= run_int<std::int32_t>("sdc_tol_i32", cfg);
    rc |= run_int<std::int64_t>("sdc_tol_i64", cfg);
    rc |= run_int<std::uint8_t>("sdc_tol_u8", cfg);
    rc |= run_int<std::uint16_t>("sdc_tol_u16", cfg);
    rc |= run_int<std::uint32_t>("sdc_tol_u32", cfg);
    rc |= run_int<std::uint64_t>("sdc_tol_u64", cfg);
    rc |= run_float<float>("sdc_tol_f32", cfg);
    rc |= run_float<double>("sdc_tol_f64", cfg);
    return rc;
}
