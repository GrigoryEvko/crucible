// FIXY-V-094 sentinel TU — crucible_fp_strict INTERFACE floor witness.
//
// Three claims this TU validates:
//
//   1. `CRUCIBLE_FP_STRICT_FLOOR` compile-definition is defined to 1.
//      The macro is propagated by the `crucible_fp_strict` INTERFACE
//      target's `target_compile_definitions(... INTERFACE
//      CRUCIBLE_FP_STRICT_FLOOR=1)`.  If THIS TU links the library
//      transitively (via crucible→crucible_fp_strict PUBLIC link) AND
//      the macro is not defined here, the floor is not actually
//      engaged for downstream consumers — silently fatal for V-091
//      F101 / F102 / F103 / F104 / F105 cross-vendor invariants.
//
//   2. The strict-FP compile-options pack is engaged.  We cannot
//      witness compile flags directly from C++ (the FTM-style
//      `__FAST_MATH__` predefined macro is the canonical way to
//      detect the inverse — its presence proves `-ffast-math` is
//      active, its absence proves the strict path).  We assert it is
//      NOT defined; combined with the explicit `-fno-fast-math` flag
//      this guarantees the umbrella is off.
//
//   3. Runtime witness — the IEEE-754 NaN/Inf machinery actually
//      survives optimization on this TU.  With `-ffinite-math-only`
//      the std::isnan branch is dead-code-eliminated and the V-093
//      canonicalize call would return wrong bits.  We feed a
//      runtime-volatile NaN payload through V-093's canonicalize and
//      verify the canonical projection actually fires.
//
// Self-cross-check with V-093: this TU and test_fixy_v_093 both link
// `crucible` (and therefore `crucible_fp_strict` transitively).  If
// the floor ever silently degrades — e.g. a future PR removes the
// PUBLIC link or moves it to PRIVATE — V-094's static_assert reds
// FIRST because it's compile-time, before V-093 even runs.

#include <crucible/fixy/fp/Canonicalize.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

// ─── Compile-time witness ──────────────────────────────────────────

#ifndef CRUCIBLE_FP_STRICT_FLOOR
#error "CRUCIBLE_FP_STRICT_FLOOR is not defined — crucible_fp_strict INTERFACE library is not transitively linked, OR target_compile_definitions did not propagate.  V-091 cross-vendor invariants are at risk."
#endif

static_assert(CRUCIBLE_FP_STRICT_FLOOR == 1,
              "CRUCIBLE_FP_STRICT_FLOOR must be defined to 1 — anything "
              "else indicates a downgrade.");

// `__FAST_MATH__` is GCC's compiler-defined macro that gets set when
// `-ffast-math` (or any flag that implies it) is active.  Its absence
// is necessary-but-not-sufficient proof the strict pack is engaged.
#ifdef __FAST_MATH__
#error "__FAST_MATH__ is defined — -ffast-math is active on this TU, but crucible_fp_strict's -fno-fast-math should suppress it.  Some PR re-enabled fast-math per-TU; check-no-ffast-math.sh should have caught this but didn't."
#endif

// `__FINITE_MATH_ONLY__` is the more specific GCC macro for the
// no-NaN-no-Inf claim that V-093 relies on NOT being true.
#if defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__ != 0
#error "__FINITE_MATH_ONLY__ is defined and non-zero — std::isnan branches will be dead-code-eliminated, V-093 canonicalize() will produce wrong results."
#endif

// ─── Runtime witness — V-093 canonicalize still fires ──────────────

void witness_canonicalize_under_floor() {
    // Runtime-volatile NaN payload to defeat constant folding.  If
    // `-ffinite-math-only` were active despite the static_assert
    // above (e.g. compiler bug, undocumented flag interaction), the
    // optimizer would replace std::isnan(x) inside canonicalize() with
    // `false` and return raw bits instead of kCanonicalQNaN64.
    volatile std::uint64_t nan_payload = 0x7FF1234567890ABCULL;
    const double nan_value = std::bit_cast<double>(nan_payload);

    const auto canonical = crucible::fixy::fp::canonicalize(nan_value);
    if (canonical != crucible::fixy::fp::kCanonicalQNaN64) {
        std::fprintf(stderr,
                     "V-094 FAIL: canonicalize(NaN) returned 0x%016lx, "
                     "expected 0x%016lx — the FP-strict floor is not "
                     "actually engaged on this TU, despite the "
                     "compile-definition being set.\n",
                     canonical, crucible::fixy::fp::kCanonicalQNaN64);
        std::abort();
    }

    // Also witness ±0 collapse — relies on signed-zeros being
    // preserved (i.e. `-fno-signed-zeros` is NOT in effect).
    volatile double neg_zero_v = -0.0;
    const double neg_zero = neg_zero_v;
    const auto raw_neg_zero = std::bit_cast<std::uint64_t>(neg_zero);
    if (raw_neg_zero == 0) {
        std::fprintf(stderr,
                     "V-094 FAIL: -0.0 bit pattern is zero on this TU "
                     "— `-fno-signed-zeros` is active, V-093's ±0 "
                     "canonicalization invariant is broken.\n");
        std::abort();
    }
    if (crucible::fixy::fp::canonicalize(neg_zero) != 0) {
        std::fprintf(stderr,
                     "V-094 FAIL: canonicalize(-0.0) returned non-zero "
                     "— ±0 → +0 projection failed.\n");
        std::abort();
    }
}

}  // namespace

int main() {
    witness_canonicalize_under_floor();
    return 0;
}
