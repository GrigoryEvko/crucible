// ═══════════════════════════════════════════════════════════════════
// prop_saturate_math_invariants — crucible::sat::{add,sub,mul}_sat
// mathematical properties under random stress.
//
// Saturate.h is a drop-in polyfill for C++26 P0543 until libstdc++
// ships __cpp_lib_saturation_arithmetic.  When that switch-over
// happens this fuzzer is the regression net: same properties, new
// backend.  It must stay green.
//
// ─── Bug classes this catches ──────────────────────────────────────
//
// 1. Miscompilation of __builtin_{add,sub,mul}_overflow on a target
//    microarchitecture (Zen 4, Sapphire Rapids, Graviton3).  Property
//    tests with fresh random (a, b) pairs each iteration exercise the
//    overflow-check paths the compiler autogenerates; a codegen
//    regression that drops the carry flag check surfaces immediately
//    as a mismatch against the naïve `a + b` reference on non-overflow
//    inputs, or as a non-MAX result on overflow inputs.
//
// 2. Sign-direction bug in signed saturation.  Saturate.h picks
//    MIN vs MAX by inspecting `a < 0` for add/sub, XOR of sign bits
//    for mul.  A typo ("a < 0" → "a > 0") passes add_sat(MAX, 1)
//    but fails add_sat(MIN, -1).  The fuzzer covers both halves.
//
// 3. Saturation direction confusion for mul_sat.  Sign of the
//    mathematical product is neg iff exactly one operand is negative.
//    Getting the boolean wrong (AND instead of XOR) passes most tests
//    but fails on (negative, positive) overflow and (negative,
//    negative) overflow — both reached by random sampling at scale.
//
// 4. Silent wrap on the overflow path.  If a future refactor drops
//    the `if (__builtin_*_overflow)` guard and just returns `r`, the
//    wrap result coincides with naïve math modulo 2^N — only the
//    saturation-specific assertions in this file catch that.
//
// 5. Commutativity violations from lazy short-circuit ordering (e.g.,
//    always checking `a < 0` before `b < 0` produces the right
//    answer for `mul_sat(a, b)` but could differ from `mul_sat(b, a)`
//    if a subtle branch evaluates operands asymmetrically).
//
// 6. Unsigned sub_sat wrapping to UINT_MAX instead of clamping to 0.
//    Standard-library implementations have historically gotten this
//    wrong; our branch returns `std::numeric_limits<T>::min()` which
//    is 0 for unsigned.  Any change must preserve that.
//
// ─── Strategy ──────────────────────────────────────────────────────
//
// Each iteration picks a random (a, b) pair drawn from a mixed
// distribution:
//   - 40% small values (|x| < 2^16) to exercise the no-overflow path
//   - 30% medium values (2^32-scale) to mix overflow frequency
//   - 30% pathological (near MIN/MAX) to force saturation
//
// The properties are checked for both uint64_t and int64_t to cover
// the signed/unsigned code paths independently.  A parallel
// uint32_t / int32_t pair verifies the template works for narrower
// types too (the only thing size-sensitive in Saturate.h is
// std::numeric_limits<T>).
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/Saturate.h>

#include <cstdint>
#include <limits>

namespace {

// ─── Reference arithmetic via builtins ────────────────────────────
//
// The naïve "truth" we differentiate against.  Returns the overflow
// flag alongside the (wrapped) result — the fuzzer uses the flag to
// decide which branch of sat_* the saturating function should follow.
template <typename T>
struct RefResult {
    T    value;
    bool overflowed;
};

template <typename T>
[[nodiscard]] constexpr RefResult<T> ref_add(T a, T b) noexcept {
    T r{};
    const bool ov = __builtin_add_overflow(a, b, &r);
    return {r, ov};
}

template <typename T>
[[nodiscard]] constexpr RefResult<T> ref_sub(T a, T b) noexcept {
    T r{};
    const bool ov = __builtin_sub_overflow(a, b, &r);
    return {r, ov};
}

template <typename T>
[[nodiscard]] constexpr RefResult<T> ref_mul(T a, T b) noexcept {
    T r{};
    const bool ov = __builtin_mul_overflow(a, b, &r);
    return {r, ov};
}

// ─── Saturation sentinel selectors ────────────────────────────────
//
// For add/sub: overflow direction depends on sign of `a` for signed
// types, always MAX for unsigned add, always 0 for unsigned sub.
// For mul: overflow direction depends on XOR of signs (signed only).

template <typename T>
[[nodiscard]] constexpr T expected_add_sat_overflow(T a, T /*b*/) noexcept {
    if constexpr (std::is_signed_v<T>) {
        return (a < T{0}) ? std::numeric_limits<T>::min()
                          : std::numeric_limits<T>::max();
    } else {
        return std::numeric_limits<T>::max();
    }
}

template <typename T>
[[nodiscard]] constexpr T expected_sub_sat_overflow(T a, T /*b*/) noexcept {
    if constexpr (std::is_signed_v<T>) {
        return (a < T{0}) ? std::numeric_limits<T>::min()
                          : std::numeric_limits<T>::max();
    } else {
        return std::numeric_limits<T>::min();  // = 0 for unsigned
    }
}

template <typename T>
[[nodiscard]] constexpr T expected_mul_sat_overflow(T a, T b) noexcept {
    if constexpr (std::is_signed_v<T>) {
        const bool neg = (a < T{0}) != (b < T{0});
        return neg ? std::numeric_limits<T>::min()
                   : std::numeric_limits<T>::max();
    } else {
        return std::numeric_limits<T>::max();
    }
}

// ─── Operand generator ────────────────────────────────────────────
//
// Mixed distribution: small / medium / pathological.  Works for both
// signed and unsigned T via static branching on signedness.

template <typename T>
[[nodiscard]] T random_operand(crucible::fuzz::prop::Rng& rng) noexcept {
    const uint32_t bucket = rng.next_below(100);
    const uint64_t bits   = rng.next64();

    if (bucket < 40) {
        // Small: fits in 16 bits.
        const auto small = static_cast<uint16_t>(bits & 0xFFFFu);
        if constexpr (std::is_signed_v<T>) {
            // Map to signed range including negatives.
            return static_cast<T>(
                static_cast<int32_t>(small) - 0x8000);
        } else {
            return static_cast<T>(small);
        }
    }
    if (bucket < 70) {
        // Medium: up to 32 bits.
        const auto med = static_cast<uint32_t>(bits & 0xFFFFFFFFu);
        if constexpr (std::is_signed_v<T>) {
            return static_cast<T>(static_cast<int32_t>(med));
        } else {
            return static_cast<T>(med);
        }
    }
    // Pathological: full-width, frequently near MIN/MAX.
    if constexpr (sizeof(T) == 8) {
        return static_cast<T>(bits);
    } else {
        return static_cast<T>(bits & 0xFFFFFFFFu);
    }
}

// ─── Property checks (one instantiation per type) ─────────────────
//
// Returns true if all invariants hold for the given (a, b).  A single
// false propagates up and triggers report_failure in the runner.

template <typename T>
[[nodiscard]] bool check_sat_props(T a, T b) noexcept {
    using namespace crucible::sat;
    constexpr T kZero = T{0};
    constexpr T kOne  = T{1};
    constexpr T kMin  = std::numeric_limits<T>::min();
    constexpr T kMax  = std::numeric_limits<T>::max();

    // ── Algebraic identities (additive / multiplicative neutral) ──
    if (add_sat<T>(a, kZero) != a) return false;
    if (add_sat<T>(kZero, a) != a) return false;
    if (sub_sat<T>(a, kZero) != a) return false;
    if (sub_sat<T>(a, a)     != kZero) return false;
    if (mul_sat<T>(a, kOne)  != a) return false;
    if (mul_sat<T>(kOne, a)  != a) return false;
    if (mul_sat<T>(a, kZero) != kZero) return false;
    if (mul_sat<T>(kZero, a) != kZero) return false;

    // ── Commutativity ────────────────────────────────────────────
    if (add_sat<T>(a, b) != add_sat<T>(b, a)) return false;
    if (mul_sat<T>(a, b) != mul_sat<T>(b, a)) return false;

    // ── Differential vs reference: add ───────────────────────────
    {
        const RefResult<T> ref = ref_add<T>(a, b);
        const T got = add_sat<T>(a, b);
        if (!ref.overflowed) {
            if (got != ref.value) return false;
        } else {
            if (got != expected_add_sat_overflow<T>(a, b)) return false;
        }
    }

    // ── Differential vs reference: sub ───────────────────────────
    {
        const RefResult<T> ref = ref_sub<T>(a, b);
        const T got = sub_sat<T>(a, b);
        if (!ref.overflowed) {
            if (got != ref.value) return false;
        } else {
            if (got != expected_sub_sat_overflow<T>(a, b)) return false;
        }
    }

    // ── Differential vs reference: mul ───────────────────────────
    {
        const RefResult<T> ref = ref_mul<T>(a, b);
        const T got = mul_sat<T>(a, b);
        if (!ref.overflowed) {
            if (got != ref.value) return false;
        } else {
            if (got != expected_mul_sat_overflow<T>(a, b)) return false;
        }
    }

    // ── Saturation at extremes ───────────────────────────────────
    //
    // These are true for ALL T and ALL (a, b) regardless of the
    // random draw — we hard-code the extreme operands each iteration
    // to guarantee the saturation path is taken every time (random
    // draws alone leave it probabilistic).
    if constexpr (std::is_unsigned_v<T>) {
        // Unsigned add: MAX + anything positive → MAX.
        if (a != kZero) {
            if (add_sat<T>(kMax, a) != kMax) return false;
            if (add_sat<T>(a, kMax) != kMax) return false;
        }
        // Unsigned sub: 0 - anything positive → 0.
        if (a != kZero) {
            if (sub_sat<T>(kZero, a) != kZero) return false;
        }
        // Unsigned mul: MAX * k (k>1) → MAX; MAX * 0 → 0; MAX * 1 → MAX.
        if (a > kOne) {
            if (mul_sat<T>(kMax, a) != kMax) return false;
            if (mul_sat<T>(a, kMax) != kMax) return false;
        }
        if (mul_sat<T>(kMax, kZero) != kZero) return false;
        if (mul_sat<T>(kMax, kOne)  != kMax)  return false;
    } else {
        // Signed add: MAX + positive → MAX; MIN + negative → MIN.
        if (a > kZero) {
            if (add_sat<T>(kMax, a) != kMax) return false;
            if (add_sat<T>(a, kMax) != kMax) return false;
        }
        if (a < kZero) {
            if (add_sat<T>(kMin, a) != kMin) return false;
            if (add_sat<T>(a, kMin) != kMin) return false;
        }
        // Signed sub: MAX - negative → MAX; MIN - positive → MIN.
        if (a < kZero && a != kMin) {
            if (sub_sat<T>(kMax, a) != kMax) return false;
        }
        if (a > kZero) {
            if (sub_sat<T>(kMin, a) != kMin) return false;
        }
        // Signed mul extreme: MIN * (-1) overflows (|MIN| > MAX) → MAX.
        if (kMin != T{0}) {
            if (mul_sat<T>(kMin, T{-1}) != kMax) return false;
        }
    }

    return true;
}

// ─── Generated input wrapper ──────────────────────────────────────
//
// Holds (a, b) for each type we exercise.  One draw per iteration
// provides coverage for four distinct instantiations.

struct Input {
    uint64_t u64_a, u64_b;
    int64_t  i64_a, i64_b;
    uint32_t u32_a, u32_b;
    int32_t  i32_a, i32_b;
};

[[nodiscard]] Input generate(crucible::fuzz::prop::Rng& rng) noexcept {
    Input in{};
    in.u64_a = random_operand<uint64_t>(rng);
    in.u64_b = random_operand<uint64_t>(rng);
    in.i64_a = random_operand<int64_t>(rng);
    in.i64_b = random_operand<int64_t>(rng);
    in.u32_a = random_operand<uint32_t>(rng);
    in.u32_b = random_operand<uint32_t>(rng);
    in.i32_a = random_operand<int32_t>(rng);
    in.i32_b = random_operand<int32_t>(rng);
    return in;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(argc, argv);

    return run("saturate math invariants (u64/i64/u32/i32)", cfg,
        [](Rng& rng) { return generate(rng); },
        [](const Input& in) {
            // Volatile barrier defeats constexpr folding of the
            // constexpr sat:: primitives.  Each iteration's (a, b)
            // must enter add_sat / sub_sat / mul_sat at RUNTIME so
            // the __builtin_*_overflow codegen is actually exercised
            // rather than constant-folded away.
            const Input* volatile p = &in;

            if (!check_sat_props<uint64_t>(p->u64_a, p->u64_b)) return false;
            if (!check_sat_props<int64_t>(p->i64_a,  p->i64_b))  return false;
            if (!check_sat_props<uint32_t>(p->u32_a, p->u32_b))  return false;
            if (!check_sat_props<int32_t>(p->i32_a,  p->i32_b))  return false;
            return true;
        });
}
