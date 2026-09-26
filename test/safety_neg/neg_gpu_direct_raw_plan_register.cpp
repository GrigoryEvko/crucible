// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A memory-region registration takes a declared plan, and
// mint_gpu_direct_mr_plan is the one function that makes one.  A plan
// built by hand does not convert.

#include <crucible/cntp/_wip/GpuDirect.h>

namespace gd = crucible::cntp::_wip::gpu_direct;

int main() {
    const gd::GpuDirectMrPlan plan{
        .gpu_base = *gd::admit_gpu_virtual_address(0x1000u),
        .bytes = *gd::admit_gpu_direct_bytes(4096),
    };
    auto result = gd::register_gpu_memory(plan);
    return result.has_value() ? 0 : 1;
}
