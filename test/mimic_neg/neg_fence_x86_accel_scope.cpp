// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The cross-trunk gate of lower_fence: an x86 fence with an accel scope.
//
// lower_fence<AcqRel, Cta, X86>() pairs a scope of the accel trunk (a PTX
// thread block) with the x86 fence dialect.  x86 emits mfence and has no
// `.cta` scope token.  fence_arch_scope_consistent returns false, and the
// cross-trunk static_assert of lower_fence fires.
//
// The arm-accel and gpu-arm fixtures hold the other two corners of the
// cross-trunk gate.

#include <crucible/mimic/Fence.h>

namespace mf = crucible::mimic;
using BS = foundation::algebra::lattices::BarrierStrength;
using MS = foundation::algebra::lattices::MemoryScope;

constexpr auto bad = mf::lower_fence<BS::AcqRel, MS::Cta, mf::FenceArch::X86>();

int main() {
    (void)bad;
    return 0;
}
