// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The strength gate of lower_fence: a device-wide GPU fence below AcqRel.
//
// lower_fence<ReleaseStore, Gpu, Gpu>() asks for a device-wide (PTX
// `.gpu`) fence with the strength ReleaseStore, which is less than AcqRel.
// A publish to a device-wide or wider scope needs acquire-release.  A
// release-only barrier makes a write visible to more threads, but it does
// not give the readers the two-sided order.  fence_strength_meets_scope
// returns false, and the strength static_assert of lower_fence fires.
//
// The pair of scope and dialect (Gpu and Gpu) agrees with the trunk rule.
// The strength clause is the only failure, so the fixture shows that the
// two gates are independent.  The three cross-trunk fixtures hold the
// other gate.

#include <crucible/mimic/Fence.h>

namespace mf = crucible::mimic;
using BS = foundation::algebra::lattices::BarrierStrength;
using MS = foundation::algebra::lattices::MemoryScope;

constexpr auto bad = mf::lower_fence<BS::ReleaseStore, MS::Gpu, mf::FenceArch::Gpu>();

int main() {
    (void)bad;
    return 0;
}
