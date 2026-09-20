// The second of the three constructors that built a pin proof out of
// nothing.  A default-constructed CpuPinned claims a pin on a thread
// that never called sched_setaffinity, and the claim is indistinguishable
// from an earned one at the point that reads it.
//
// It is deleted with a reason rather than made private, because a reader
// who reaches for it needs to be told where the proof comes from.  The
// deletion message names fixy::sched::mint_affinity for that reason, and
// the second required diagnostic below is that message: a deletion whose
// reason went missing would stop being an explanation.
//
// See neg_os_cpu_pinned_value_ctor.cpp for why a forged pin matters.

#include <fixy/os/CpuPinned.h>

namespace ml = foundation::algebra::lattices;

namespace {
using Pin = fixy::CpuPinned<ml::AffinityMask::single(0), fixy::PinningPosture::PinnedExplicit, int>;
}  // namespace

int main() {
    Pin forged;
    (void)forged;
    return 0;
}
