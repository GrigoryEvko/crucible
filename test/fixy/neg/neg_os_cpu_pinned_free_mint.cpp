// fixy::mint_cpu_pinned was a §XXI-shaped factory with no ctx and no
// evidence: it forwarded its arguments into the private constructor and
// returned a pin proof for any mask and any posture the caller named.
// It is gone, and fixy::sched::mint_affinity is the only mint that
// produces a CpuPinned.
//
// This fixture is the weakest of the five on its own, because "not a
// member" would also be the diagnostic if the header stopped existing.
// It earns its place beside the other four: those fail on the private
// constructor and the deleted copy, so they cannot pass with the header
// gone, and together the five say that the name is absent AND that no
// route to the constructor reopened.
//
// See neg_os_cpu_pinned_value_ctor.cpp for why a forged pin matters.

#include <fixy/os/CpuPinned.h>

namespace ml = foundation::algebra::lattices;

int main() {
    auto forged =
        fixy::mint_cpu_pinned<ml::AffinityMask::single(0), fixy::PinningPosture::PinnedExplicit, int>(0);
    (void)forged;
    return 0;
}
