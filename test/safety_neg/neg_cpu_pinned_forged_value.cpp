// CpuPinned asserts that the thread holding it was pinned to its mask.
// The value constructor was public, so an int became a pin proof on a
// thread that never called sched_setaffinity.  A forged single-core pin
// passes the TSC reader gate, and two counter reads then come from two
// cores.  The constructor is private now, and only mint_affinity reaches
// the builder that calls it.

#include <crucible/safety/_CpuPinned.h>

#include <crucible/algebra/lattices/_AffinityLattice.h>

using AffinityMask = ::crucible::algebra::lattices::AffinityMask;

int main() {
    ::crucible::safety::CpuPinned<AffinityMask::single(0), ::crucible::safety::PinningPosture::PinnedExplicit, int>
        forged{0};
    return forged.peek();
}
