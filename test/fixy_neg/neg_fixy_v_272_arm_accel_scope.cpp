// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-272 lower_fence gate fixture (mismatch class: V402 ARM×ACCEL).
//
// Violation: lower_fence<AcqRel, Gpu, Arm>() pairs an accel-trunk (PTX
// device-wide) scope with the aarch64 DMB dialect.  DMB has ISH/OSH/SY
// shareability domains but no `.gpu` device scope, so
// fence_arch_scope_consistent rejects — the V402 trunk static_assert fires.
//
// Distinct from the x86-accel fixture (Arm host dialect, not X86) and the
// gpu-arm fixture (the cross-trunk direction is reversed).
//
// Expected diagnostic: "static assertion failed" / cross-trunk / V402.

#include <crucible/mimic/Fence.h>

namespace mf = crucible::mimic;
using BS = crucible::algebra::lattices::BarrierStrength;
using MS = crucible::algebra::lattices::MemoryScope;

constexpr auto bad = mf::lower_fence<BS::AcqRel, MS::Gpu, mf::FenceArch::Arm>();

int main() {
    (void)bad;
    return 0;
}
