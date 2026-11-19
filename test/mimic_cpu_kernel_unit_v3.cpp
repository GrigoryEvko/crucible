// The unit of test_mimic_cpu_kernels for the level x86-64-v3.  The build
// compiles this file with -march=x86-64-v3.

#include "mimic_cpu_kernel_units.h"

namespace crucible::test {

KernelDigests kernel_digests_of_v3_unit() noexcept { return kernel_digests_of_this_unit<KernelUnit::V3>(); }

}  // namespace crucible::test
