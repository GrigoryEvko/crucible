#pragma once

// The units of test_core_product for the levels x86-64-v3 and x86-64-v4.
// The build compiles product_unit_v3.cpp with -march=x86-64-v3 (8 float
// lanes) and product_unit_v4.cpp with -march=x86-64-v4 (16 float lanes).  The
// unit of the test takes the target of its preset: 4 float lanes with no
// -march.  Each unit computes the digests of product_forms.h with its own
// bodies, and each digest is the same.  A unit runs only on a host of its
// level.

#include "product_forms.h"

namespace crucible::test {

[[nodiscard]] ProductDigests product_digests_of_v3_unit() noexcept;
[[nodiscard]] ProductDigests product_digests_of_v4_unit() noexcept;

}  // namespace crucible::test
