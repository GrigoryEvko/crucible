// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-132. Runtime MR registration requires a
// _wip::gpu_direct::wip_source::GpuDirect-declared plan, not a raw aggregate.

#include <crucible/cntp/_wip/GpuDirect.h>

namespace gd = crucible::cntp::_wip::gpu_direct;

int main() {
    gd::GpuDirectMrPlan plan{};
    auto result = gd::register_gpu_memory(plan);
    return result.has_value() ? 0 : 1;
}
