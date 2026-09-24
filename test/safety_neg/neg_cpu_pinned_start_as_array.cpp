// The checked lifetime start refuses an old-tree pin proof.  Its move
// constructor is user-provided and its copy is deleted, so it is not an
// implicit-lifetime type, and start_as_array refuses it at its constraint.

#include <crucible/safety/_CpuPinned.h>
#include <foundation/Lifetime.h>

#include <crucible/algebra/lattices/_AffinityLattice.h>

using AffinityMask = ::crucible::algebra::lattices::AffinityMask;

int main() {
    using Pin = ::crucible::safety::CpuPinned<AffinityMask::single(0), ::crucible::safety::PinningPosture::PinnedExplicit,
                                              int>;
    alignas(Pin) unsigned char bytes[sizeof(Pin)]{};
    auto const forged = ::foundation::lifetime::start_as_array<Pin>(bytes, 1);
    return static_cast<int>(forged.size());
}
