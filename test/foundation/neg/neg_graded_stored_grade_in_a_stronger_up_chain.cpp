// A purer source is the stronger claim in the determinism chain.  A grade
// stored beside a value in that order can let weaken() move a value read
// from a nondeterministic syscall up to Pure.  The chain states that its
// up is the stronger claim, and Graded refuses it at the template head.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>

int main() {
    namespace fa = ::foundation::algebra;
    namespace fl = ::foundation::algebra::lattices;
    using Authority = fa::detail::self_test_authority;
    fa::Graded<fa::ModalityKind::Absolute, fl::DetSafeLattice, int> const read{
        Authority::key(), 5, fl::DetSafeTier::NonDeterministicSyscall};
    return read.peek();
}
