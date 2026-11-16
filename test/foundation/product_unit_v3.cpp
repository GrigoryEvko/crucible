// The unit of test_core_product for the level x86-64-v3.  The build compiles
// this file with -march=x86-64-v3.

#include "product_units.h"

namespace crucible::test {

ProductDigests product_digests_of_v3_unit() noexcept { return digests_of_this_unit(); }

}  // namespace crucible::test
