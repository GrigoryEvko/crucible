// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A memory-region registration takes a declared plan, and
// mint_gpu_direct_mr_plan is the one function that makes one.  A plan
// built by hand does not convert.

#include <crucible/cntp/_wip/GpuDirect.h>

// The register path is a stub, and its deprecation is not the error under
// test.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace gd = crucible::cntp::_wip::gpu_direct;

int main() {
    const gd::GpuDirectMrPlan plan{
        .gpu_base = gd::admit_gpu_virtual_address(0x1000u).expect("the address is not null"),
        .bytes = gd::admit_gpu_direct_bytes(4096).expect("the byte count is positive"),
    };
    auto result = gd::register_gpu_memory(plan);
    return result.is_ok() ? 0 : 1;
}
