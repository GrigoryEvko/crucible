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
    using Authority = fa::detail::self_test_authority;
    fa::Graded<fa::ModalityKind::Absolute, fl::DualLattice<fl::PeakBytesLattice>, int> const measured{
        Authority::key(), 1, fl::PeakBytesLattice::bottom()};
    return measured.peek();
}
