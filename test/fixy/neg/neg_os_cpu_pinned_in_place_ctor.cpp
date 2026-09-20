// The third of the three constructors that built a pin proof out of
// nothing.  It is removed rather than hidden: it existed so that a
// non-movable Unit could be built in place, and the sole friend fixes
// Unit to PinProofUnit, which is an int.
//
// Closing two routes and leaving the third open is the failure this
// discipline keeps finding, so this route gets its own witness rather
// than resting on the other two.
//
// See neg_os_cpu_pinned_value_ctor.cpp for why a forged pin matters.

#include <fixy/os/CpuPinned.h>

#include <utility>

namespace ml = foundation::algebra::lattices;

namespace {
using Pin = fixy::CpuPinned<ml::AffinityMask::single(0), fixy::PinningPosture::PinnedExplicit, int>;
}  // namespace

int main() {
    Pin forged{std::in_place, 0};
    (void)forged;
    return 0;
}
