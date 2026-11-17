// The unit of test_core_fma with the exact form.  On x86_64 the build
// compiles this file with -march=x86-64, which has no FMA, so fp::fma is the
// exact form in integers here, also in a build at x86-64-v3.

#include "fma_units.h"

#include <foundation/core/Float.h>

namespace crucible::test {

float fma_of_baseline_unit(float left, float right, float addend) noexcept {
    return ::foundation::core::fp::fma(left, right, addend);
}

double fma_of_baseline_unit(double left, double right, double addend) noexcept {
    return ::foundation::core::fp::fma(left, right, addend);
}

bool is_baseline_unit_exact() noexcept {
#if defined(__FMA__) || defined(__aarch64__)
    return false;
#else
    return true;
#endif
}

}  // namespace crucible::test
