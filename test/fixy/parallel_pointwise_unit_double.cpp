// A unit of test_concurrent_parallel_pointwise: the cases of two inputs over
// planes of doubles.  parallel_pointwise_rig.h tells why the test compiles in
// more than one unit.

#include "parallel_pointwise_rig.h"

namespace test_parallel_pointwise {

void test_the_double_two_inputs(::fixy::TestRunnerCtx const& test, BgCtx const& ctx) noexcept {
    two_inputs<double>(test, ctx, "double");
}

}  // namespace test_parallel_pointwise
