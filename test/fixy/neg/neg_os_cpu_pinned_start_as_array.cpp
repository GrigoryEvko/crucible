// The checked lifetime start refuses a pin proof.  Its move constructor is
// user-provided and its copy is deleted, so it is not an implicit-lifetime
// type, and start_as_array refuses it at its constraint.  A pin started
// over bytes would claim a sched_setaffinity call that nobody made.

#include <fixy/os/CpuPinned.h>
#include <foundation/Lifetime.h>

namespace ml = foundation::algebra::lattices;

int main() {
    using Pin = fixy::CpuPinned<ml::AffinityMask::single(0), fixy::PinningPosture::PinnedExplicit, int>;
    alignas(Pin) unsigned char bytes[sizeof(Pin)]{};
    auto const forged = ::foundation::lifetime::start_as_array<Pin>(bytes, 1);
    return static_cast<int>(forged.size());
}
