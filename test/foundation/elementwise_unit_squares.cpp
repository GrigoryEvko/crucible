// A unit of test_core_elementwise: the tests of SquareOfDifference of floats
// and of doubles.  elementwise_rig.h tells why the tests compile in more than
// one unit.

#include "elementwise_rig.h"

namespace crucible::test {

void test_the_float_square_of_a_difference_is_inside_its_bound() {
    test_the_square_of_a_difference_is_inside_its_bound<float>();
}

void test_the_double_square_of_a_difference_is_inside_its_bound() {
    test_the_square_of_a_difference_is_inside_its_bound<double>();
}

}  // namespace crucible::test
