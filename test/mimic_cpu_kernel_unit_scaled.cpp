// The second unit of test_mimic_cpu_kernels with the target of its preset:
// the kernel scale_product_of_difference alone.

#include "mimic_cpu_kernel_forms.h"

namespace crucible::test {

void scale_product_of_difference_in_build_unit(KernelCtx const& ctx, Matrix<float>& out, ScaledMinuend minuend,
                                               ScaledSubtrahend subtrahend, ScaledScale scale,
                                               ScaledFactor factor) noexcept {
    run_into(ctx, out, cpu::scale_product_of_difference, minuend, subtrahend, scale, factor);
}

}  // namespace crucible::test
