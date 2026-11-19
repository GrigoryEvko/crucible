// A unit of test_pointwise: the property of the multiply-add of doubles.
// pointwise_rig.h tells why the tests compile in more than one unit.

#include "pointwise_rig.h"

namespace test_pointwise {

void test_the_double_multiply_add(::fixy::TestRunnerCtx const& test) noexcept {
    pointwise_agrees_with_walk<double, 3>(test, "double multiply-add", kSeed + 5, MultiplyAdd{});
}

}  // namespace test_pointwise
