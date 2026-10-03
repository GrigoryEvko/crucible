// A use counter reads up as the weaker claim: more bytes used promise
// less.  Its order dual reads up as the stronger claim, so weaken() on it
// would claim fewer bytes than were used.  Graded refuses the dual of a
// use counter for the same reason that it refuses a version counter.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>

int main() {
    namespace fa = ::foundation::algebra;
    namespace fl = ::foundation::algebra::lattices;
    // The size is the one use of the type.  The template head refuses the
    // type, so a construction would give a second error for the same cause.
    static_cast<void>(sizeof(fa::Graded<fa::ModalityKind::Absolute, fl::DualLattice<fl::PeakBytesLattice>, int>));
    return 0;
}
