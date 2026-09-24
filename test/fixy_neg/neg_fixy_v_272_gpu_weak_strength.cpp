// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-272 lower_fence gate fixture (mismatch class: V401 WEAK-STRENGTH).
//
// Violation: lower_fence<ReleaseStore, Gpu, Gpu>() requests a device-wide
// (PTX `.gpu`) fence guarded only by ReleaseStore (< AcqRel).  A
// device-or-wider publish needs at least acquire-release to make
// cross-CTA / cross-device writes visible; a release-only barrier widens
// visibility without establishing the two-sided ordering device readers
// require.  fence_strength_meets_scope rejects — the V401 static_assert
// fires.  The (scope, arch) pair here is trunk-CONSISTENT (Gpu×Gpu), so
// the V401 strength clause is the FIRST and only failure — proving the two
// gates are independent.
//
// Distinct from the trunk fixtures (those fail V402; this fails V401).
//
// Expected diagnostic: "static assertion failed" / AcqRel / V401.

#include <crucible/mimic/Fence.h>

namespace mf = crucible::mimic;
using BS = crucible::algebra::lattices::BarrierStrength;
using MS = crucible::algebra::lattices::MemoryScope;

constexpr auto bad = mf::lower_fence<BS::ReleaseStore, MS::Gpu, mf::FenceArch::Gpu>();

int main() {
    (void)bad;
    return 0;
}
