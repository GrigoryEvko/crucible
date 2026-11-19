#pragma once

// The units of test_core_product for the levels x86-64-v3 and x86-64-v4.
// The build compiles product_unit_v3.cpp with -march=x86-64-v3 (8 float
// lanes) and product_unit_v4.cpp with -march=x86-64-v4 (16 float lanes).  The
// unit of the test takes the target of its preset.  Each unit computes the
// digests of product_forms.h with its own
// bodies, and each digest is the same.  A unit runs only on a host of its
// level.
//
// The build compiles the unit of a level only when the level of the build is
// a different level (test/foundation/CMakeLists.txt).  A build at x86-64-v3,
// such as the build of the preset default, gives the unit of the test the
// bodies of v3, so the unit of v3 would compile the same bodies again.  The
// definitions CRUCIBLE_TEST_HAS_V3_UNIT and CRUCIBLE_TEST_HAS_V4_UNIT tell the
// test which units the build has.

#include "product_forms.h"

namespace crucible::test {

[[nodiscard]] ProductDigests product_digests_of_v3_unit() noexcept;
[[nodiscard]] ProductDigests product_digests_of_v4_unit() noexcept;

}  // namespace crucible::test
