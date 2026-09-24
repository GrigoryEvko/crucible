// A counter that exists cannot be edited in place.  A write to the count
// would lower an epoch that other code already compared against, so the
// count is private, and only the doors of the lattice produce a counter.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    fl::Epoch epoch = fl::EpochLattice::bottom();
    epoch.value_ = 1;
    return static_cast<int>(epoch.raw());
}
