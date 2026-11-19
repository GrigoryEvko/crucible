#pragma once

// The units of test_mimic_cpu_kernels for the levels x86-64-v3 and
// x86-64-v4.  The build compiles mimic_cpu_kernel_unit_v3.cpp and
// mimic_cpu_kernel_unit_v3_scaled.cpp with -march=x86-64-v3, and the two
// units of v4 with -march=x86-64-v4.  The unit of the test takes the target
// of its preset.  Each unit computes the digests of mimic_cpu_kernel_forms.h
// with its own bodies, and each digest is the same.  A unit runs only on a
// host of its level.
//
// The build compiles the units of a level only when the level of the build
// is a different level (test/CMakeLists.txt).  A build at x86-64-v3, such as
// the build of the preset default, gives the unit of the test the bodies of
// v3, so the units of v3 would compile the same bodies again.  The
// definitions CRUCIBLE_TEST_HAS_V3_UNIT and CRUCIBLE_TEST_HAS_V4_UNIT tell the
// test which units the build has.

#include "mimic_cpu_kernel_forms.h"

namespace crucible::test {

[[nodiscard]] KernelDigests kernel_digests_of_v3_unit() noexcept;
[[nodiscard]] KernelDigests kernel_digests_of_v4_unit() noexcept;

}  // namespace crucible::test
