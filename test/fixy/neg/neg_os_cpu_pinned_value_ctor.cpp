// CpuPinned<Mask, Posture, T> asserts that the thread holding it was
// pinned to Mask.  The value constructor asserted nothing: it took an
// int and handed back a proof, on a thread that never called
// sched_setaffinity.
//
// The forgery is not academic.  A forged
// CpuPinned<single(0), PinnedExplicit, T> satisfies IsSingletonCpuPin,
// which is the whole gate on fixy::time::TscReader.  The timestamp
// counter is per-core, so a reader admitted on a forged pin compares
// counters read on different cores.  Nothing crashes and nothing is
// diagnosed — the replay simply stops reproducing, which is a silent
// DetSafe break.
//
// The constructor is private now and fixy::sched::mint_affinity is its
// sole friend.  This fixture is the standing witness that it stayed
// private.  Its four siblings cover the other routes: the default
// constructor, the in_place constructor, the free mint, and the copy.

#include <fixy/os/CpuPinned.h>

namespace ml = foundation::algebra::lattices;

namespace {
using Pin = fixy::CpuPinned<ml::AffinityMask::single(0), fixy::PinningPosture::PinnedExplicit, int>;
}  // namespace

int main() {
    Pin forged{0};
    (void)forged;
    return 0;
}
