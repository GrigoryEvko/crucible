// A generation cannot be built from an epoch.  No constructor of a count
// takes an integer, and neither axis converts to a count or to the other
// axis, so the retag has no constructor to call.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    fl::Epoch const epoch = fl::EpochLattice::bottom();
    fl::Generation const generation{epoch};
    return static_cast<int>(generation.raw());
}
