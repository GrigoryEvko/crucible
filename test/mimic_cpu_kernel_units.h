#pragma once

// The units of test_mimic_cpu_kernels for the levels x86-64-v3 and
// x86-64-v4.  The build compiles mimic_cpu_kernel_unit_v3.cpp with
// -march=x86-64-v3 and mimic_cpu_kernel_unit_v4.cpp with -march=x86-64-v4.
// The unit of the test takes the target of its preset.  Each unit computes
// the digests of mimic_cpu_kernel_forms.h with its own bodies, and each
// digest is the same.  A unit runs only on a host of its level.

#include "mimic_cpu_kernel_forms.h"

namespace crucible::test {

[[nodiscard]] KernelDigests kernel_digests_of_v3_unit() noexcept;
[[nodiscard]] KernelDigests kernel_digests_of_v4_unit() noexcept;

}  // namespace crucible::test
