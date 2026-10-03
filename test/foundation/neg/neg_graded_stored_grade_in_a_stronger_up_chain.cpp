// A purer source is the stronger claim in the determinism chain.  A grade
// stored beside a value in that order can let weaken() move a value read
// from a nondeterministic syscall up to Pure.  The chain states that its
// up is the stronger claim, and Graded refuses it at the template head.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>

int main() {
    namespace fa = ::foundation::algebra;
    namespace fl = ::foundation::algebra::lattices;
    // The size is the one use of the type.  The template head refuses the
    // type, so a construction would give a second error for the same cause.
    static_cast<void>(sizeof(fa::Graded<fa::ModalityKind::Absolute, fl::DetSafeLattice, int>));
    return 0;
}
