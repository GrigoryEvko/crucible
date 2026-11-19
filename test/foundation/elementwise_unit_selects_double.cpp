// A unit of test_core_elementwise: the test of the selects and the copy of
// doubles.  elementwise_rig.h tells why the tests compile in more than one
// unit.

#include "elementwise_rig.h"

namespace crucible::test {

void test_the_double_selects_keep_their_bits() { test_the_selects_keep_their_bits<double>(); }

}  // namespace crucible::test
