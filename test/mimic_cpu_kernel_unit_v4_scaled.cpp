// The second unit of test_mimic_cpu_kernels for the level x86-64-v4: the
// kernel scale_product_of_difference alone.  The build compiles this file
// with -march=x86-64-v4.

#include "mimic_cpu_kernel_forms.h"

namespace crucible::test {

void scale_product_of_difference_in_v4_unit(KernelCtx const& ctx, Matrix<float>& out, ScaledMinuend minuend,
                                            ScaledSubtrahend subtrahend, ScaledScale scale,
                                            ScaledFactor factor) noexcept {
    run_into(ctx, out, cpu::scale_product_of_difference, minuend, subtrahend, scale, factor);
}

}  // namespace crucible::test
