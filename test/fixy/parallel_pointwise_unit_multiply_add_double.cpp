// A unit of test_concurrent_parallel_pointwise: the multiply-add of doubles
// with three inputs.  parallel_pointwise_rig.h tells why the test compiles in
// more than one unit.

#include "parallel_pointwise_rig.h"

namespace test_parallel_pointwise {

bool each_double_split_agrees(BgCtx const& ctx, Case<2, 3> const& drawn, MultiplyAdd op) noexcept {
    return each_split_agrees<double>(ctx, drawn, op);
}

}  // namespace test_parallel_pointwise
