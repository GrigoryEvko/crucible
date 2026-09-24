// Two versions have no sum.  The saturating sum exists on a use axis, where
// the use of two stages in sequence is the sum of their uses, and a sum of
// epochs would state a version that no membership change produced.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    fl::Epoch const epoch = fl::EpochLattice::successor(fl::EpochLattice::bottom());
    return static_cast<int>(fl::EpochLattice::saturating_sum(epoch, epoch).raw());
}
