// The join of one axis takes that axis only.  An epoch joined with a
// generation would put a restart count where a membership count belongs.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    fl::Epoch const epoch{9};
    fl::Generation const generation{3};
    return static_cast<int>(fl::EpochLattice::join(epoch, generation).raw());
}
