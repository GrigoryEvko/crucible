// A unit of test_core_elementwise: the test of Sum, Quotient and
// FusedMultiplyAdd of doubles.  elementwise_rig.h tells why the tests compile
// in more than one unit.

#include "elementwise_rig.h"

namespace crucible::test {

void test_the_double_single_rounds_are_exact() { test_the_single_rounds_are_exact<double>(); }

}  // namespace crucible::test
