// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The cross-trunk gate of lower_fence: an Arm fence with an accel scope.
//
// lower_fence<AcqRel, Gpu, Arm>() pairs a scope of the accel trunk (PTX
// device-wide) with the aarch64 DMB dialect.  DMB has the ISH, OSH and SY
// shareability domains, and it has no `.gpu` device scope.
// fence_arch_scope_consistent returns false, and the cross-trunk
// static_assert of lower_fence fires.
//
// The x86-accel fixture has an x86 dialect, and the gpu-arm fixture has
// the opposite direction across the trunks.

#include <crucible/mimic/Fence.h>

namespace mf = crucible::mimic;
using BS = foundation::algebra::lattices::BarrierStrength;
using MS = foundation::algebra::lattices::MemoryScope;

constexpr auto bad = mf::lower_fence<BS::AcqRel, MS::Gpu, mf::FenceArch::Arm>();

int main() {
    (void)bad;
    return 0;
}
