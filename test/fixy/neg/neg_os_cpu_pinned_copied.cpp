// A pin proof is move-only, and this is the fourth route to a second
// one.  Copying an earned pin would let two readers hold a proof of the
// same pin, and the second reader can be on another thread — one that
// never pinned, and whose timestamp counter is a different counter.
//
// Deleting the copy constructor is what makes a CpuPinned a claim about
// ONE thread rather than a value that travels.  The move operations stay
// public because moving transfers the claim rather than duplicating it.
//
// The duplicating function is never called.  The error is at the copy,
// so it is reported whether or not a pin was ever earned, which is what
// lets this fixture run without a sched_setaffinity.
//
// See neg_os_cpu_pinned_value_ctor.cpp for why a forged pin matters.

#include <fixy/os/CpuPinned.h>

namespace ml = foundation::algebra::lattices;

namespace {
using Pin = fixy::CpuPinned<ml::AffinityMask::single(0), fixy::PinningPosture::PinnedExplicit, int>;

[[maybe_unused]] void duplicate(const Pin& earned) {
    Pin second{earned};
    (void)second;
}
}  // namespace

int main() { return 0; }
