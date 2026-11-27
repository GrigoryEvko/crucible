// The unit of the fma sweep with no fused multiply-add.  bench/CMakeLists.txt
// compiles this file for x86-64, so fp::fma here is the form in doubles of
// foundation/core/Float.h.

#include "fp_sweep_fma.h"

#include <foundation/core/Float.h>

namespace fp_sweep {

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

}  // namespace fp_sweep
