// The unit of test_core_product for the level x86-64-v4.  The build compiles
// this file with -march=x86-64-v4.

#include "product_units.h"

namespace crucible::test {

ProductDigests product_digests_of_v4_unit() noexcept { return digests_of_this_unit(); }

}  // namespace crucible::test
