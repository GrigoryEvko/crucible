// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The cross-trunk gate of lower_fence: a GPU fence with an Arm scope.
//
// lower_fence<AcqRel, Inner, Gpu>() pairs an Arm shareability scope (DMB
// ISH) with the PTX fence dialect.  PTX has the cta, cluster, gpu and sys
// scopes, and it has no inner-shareable domain.
// fence_arch_scope_consistent returns false, and the cross-trunk
// static_assert of lower_fence fires.
//
// This is the third corner of the cross-trunk gate, after the x86-accel
// and arm-accel fixtures.

#include <crucible/mimic/Fence.h>

namespace mf = crucible::mimic;
using BS = foundation::algebra::lattices::BarrierStrength;
using MS = foundation::algebra::lattices::MemoryScope;

constexpr auto bad = mf::lower_fence<BS::AcqRel, MS::Inner, mf::FenceArch::Gpu>();

int main() {
    (void)bad;
    return 0;
}
