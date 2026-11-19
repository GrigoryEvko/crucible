// A unit of test_pointwise: the directed cases of floats, and the properties
// of the add, the divide, the negate, the copy and the scalar add of floats.
// pointwise_rig.h tells why the tests compile in more than one unit.

#include "pointwise_rig.h"

namespace test_pointwise {

void test_the_float_cases(::fixy::TestRunnerCtx const& test) noexcept {
    nan_rule_on_each_path<float>(test, "float");
    tile_classes<float>(test, "float");
    step_two_ends_the_run<float>(test, "float");
    pointwise_agrees_with_walk<float, 2>(test, "float add", kSeed, Add{});
    pointwise_agrees_with_walk<float, 2>(test, "float divide", kSeed + 2, Divide{});
    pointwise_agrees_with_walk<float, 1>(test, "float negate", kSeed + 3, Negate{});
    pointwise_agrees_with_walk<float, 1>(test, "float copy that keeps its bits", kSeed + 10, Copy{});
    pointwise_agrees_with_walk<float, 2>(test, "float add on the scalar path", kSeed + 12, ScalarAdd{});
}

}  // namespace test_pointwise
