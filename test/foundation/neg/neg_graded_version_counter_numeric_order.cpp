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
    // The size is the one use of the type.  The template head refuses the
    // type, so a construction would give a second error for the same cause.
    static_cast<void>(sizeof(fa::Graded<fa::ModalityKind::Absolute, fl::EpochLattice, int>));
    return 0;
}
