// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-272 lower_fence gate fixture (mismatch class: V402 X86×ACCEL).
//
// Violation: lower_fence<AcqRel, Cta, X86>() pairs an accel-trunk (PTX
// thread-block) scope with the x86 fence dialect.  x86 emits mfence and
// has no `.cta` scope token, so fence_arch_scope_consistent rejects —
// the V402 trunk static_assert fires.
//
// Distinct from the arm-accel and gpu-arm fixtures (this is the X86 host
// dialect meeting an accel scope, not Arm/Gpu).
//
// Expected diagnostic: "static assertion failed" / cross-trunk / V402.

#include <crucible/mimic/Fence.h>

namespace mf = crucible::mimic;
using BS = crucible::algebra::lattices::BarrierStrength;
using MS = crucible::algebra::lattices::MemoryScope;

constexpr auto bad = mf::lower_fence<BS::AcqRel, MS::Cta, mf::FenceArch::X86>();

int main() {
    (void)bad;
    return 0;
}
