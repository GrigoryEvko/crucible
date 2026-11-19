// A unit of test_pointwise: the property of the multiply-add of floats.
// pointwise_rig.h tells why the tests compile in more than one unit.

#include "pointwise_rig.h"

namespace test_pointwise {

void test_the_float_multiply_add(::fixy::TestRunnerCtx const& test) noexcept {
    pointwise_agrees_with_walk<float, 3>(test, "float multiply-add", kSeed + 1, MultiplyAdd{});
}

}  // namespace test_pointwise
