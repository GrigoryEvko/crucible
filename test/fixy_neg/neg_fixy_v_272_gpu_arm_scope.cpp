// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-272 lower_fence gate fixture (mismatch class: V402 GPU×ARM).
//
// Violation: lower_fence<AcqRel, Inner, Gpu>() pairs an ARM-shareability
// (DMB ISH) scope with the PTX/GPU fence dialect.  PTX has cta/cluster/
// gpu/sys scopes but no inner-shareable domain, so
// fence_arch_scope_consistent rejects — the V402 trunk static_assert fires.
//
// Distinct from the x86-accel and arm-accel fixtures (this is the GPU
// dialect meeting an ARM-trunk scope — the third cross-trunk corner).
//
// Expected diagnostic: "static assertion failed" / cross-trunk / V402.

#include <crucible/mimic/Fence.h>

namespace mf = crucible::mimic;
using BS = crucible::algebra::lattices::BarrierStrength;
using MS = crucible::algebra::lattices::MemoryScope;

constexpr auto bad = mf::lower_fence<BS::AcqRel, MS::Inner, mf::FenceArch::Gpu>();

int main() {
    (void)bad;
    return 0;
}
