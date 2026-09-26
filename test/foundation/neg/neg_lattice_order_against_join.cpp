// A lattice whose leq runs against its join and meet satisfies every law
// that compares elements through leq alone.  The law that leq(a, b) holds
// exactly when join(a, b) is b refuses it, so the verify helpers, which
// are constrained on Lattice, have no candidate for it.

#include <foundation/algebra/Lattice.h>

namespace fa = ::foundation::algebra;
using Inverted = fa::detail::lattice_self_test::InvertedOrder;

static_assert(fa::verify_lattice_axioms_at<Inverted>(0, 1, 3));

int main() { return 0; }
