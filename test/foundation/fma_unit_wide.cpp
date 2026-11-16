// The unit of test_core_fma with a fused multiply-add.  On x86_64 the build
// compiles this file with -march=x86-64-v4, so fp::fma is the instruction
// here.

#include "fma_units.h"

#include <foundation/core/Float.h>

namespace crucible::test {

float fma_of_wide_unit(float left, float right, float addend) noexcept {
    return ::foundation::core::fp::fma(left, right, addend);
}

double fma_of_wide_unit(double left, double right, double addend) noexcept {
    return ::foundation::core::fp::fma(left, right, addend);
}

bool is_wide_unit_fused() noexcept {
#if defined(__FMA__) || defined(__aarch64__)
    return true;
#else
    return false;
#endif
}

}  // namespace crucible::test
