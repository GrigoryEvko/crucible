// A unit of test_pointwise: the directed cases of doubles, and the
// properties of the negate that keeps its bits and of the add of doubles.
// pointwise_rig.h tells why the tests compile in more than one unit.

#include "pointwise_rig.h"

namespace test_pointwise {

void test_the_double_cases(::fixy::TestRunnerCtx const& test) noexcept {
    nan_rule_on_each_path<double>(test, "double");
    tile_classes<double>(test, "double");
    step_two_ends_the_run<double>(test, "double");
    pointwise_agrees_with_walk<double, 1>(test, "double negate that keeps its bits", kSeed + 11, KeptNegate{});
    pointwise_agrees_with_walk<double, 2>(test, "double add", kSeed + 4, Add{});
}

}  // namespace test_pointwise
