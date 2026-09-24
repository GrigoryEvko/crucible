// Graded reads its up direction as the weaker claim.  A newer epoch is
// the stronger claim, so a Graded over the numeric epoch order would let
// weaken() mark a stale value as fresh.  The epoch states that its up is
// the stronger claim, and Graded refuses it at the template head.  The
// order dual is the lattice that grades a value by its version.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>

int main() {
    namespace fa = ::foundation::algebra;
    namespace fl = ::foundation::algebra::lattices;
    fa::Graded<fa::ModalityKind::Absolute, fl::EpochLattice, int> const stale{1, fl::EpochLattice::bottom()};
    return stale.peek();
}
