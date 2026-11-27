// The unit of test_core_fma with no fused multiply-add.  On x86_64 the build
// compiles this file with -march=x86-64, which has no FMA, so fp::fma is the
// form in doubles here (fma_by_double_ and fma_by_pairs_ of Float.h), also in
// a build at x86-64-v3.

#include "fma_units.h"

#include <foundation/core/Float.h>

namespace crucible::test {

float fma_of_baseline_unit(float left, float right, float addend) noexcept {
    return ::foundation::core::fp::fma(left, right, addend);
}

double fma_of_baseline_unit(double left, double right, double addend) noexcept {
    return ::foundation::core::fp::fma(left, right, addend);
}

// The condition of the forms in doubles in fp::fma: no fused multiply-add,
// the floor crucible_fp_strict, and no fast math.
bool is_baseline_unit_unfused() noexcept {
#if !defined(__FMA__) && !defined(__aarch64__) && defined(CRUCIBLE_FP_STRICT_FLOOR) && !defined(__FAST_MATH__)
    return true;
#else
    return false;
#endif
}

}  // namespace crucible::test
