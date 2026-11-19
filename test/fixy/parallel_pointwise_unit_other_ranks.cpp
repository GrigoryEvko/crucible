// A unit of test_concurrent_parallel_pointwise: the add of floats over views
// of rank one and of rank three.  parallel_pointwise_rig.h tells why the test
// compiles in more than one unit.

#include "parallel_pointwise_rig.h"

namespace test_parallel_pointwise {

bool each_float_split_agrees(BgCtx const& ctx, Case<1, 2> const& drawn, Add op) noexcept {
    return each_split_agrees<float>(ctx, drawn, op);
}

bool each_float_split_agrees(BgCtx const& ctx, Case<3, 2> const& drawn, Add op) noexcept {
    return each_split_agrees<float>(ctx, drawn, op);
}

}  // namespace test_parallel_pointwise
