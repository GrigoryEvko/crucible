// A product moves every component at once.  A product of the epoch's dual
// and the generation in its numeric order would let weaken() mark the
// generation newer than the value's own, so the product reads up as the
// stronger claim, and Graded refuses it.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>

int main() {
    namespace fa = ::foundation::algebra;
    namespace fl = ::foundation::algebra::lattices;
    using HalfTurned = fl::ProductLattice<fl::DualLattice<fl::EpochLattice>, fl::GenerationLattice>;
    fa::Graded<fa::ModalityKind::Absolute, HalfTurned, int> const value{1, {fl::EpochLattice::bottom(), fl::GenerationLattice::bottom()}};
    return value.peek();
}
