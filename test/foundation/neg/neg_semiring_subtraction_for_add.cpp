// Subtraction for add has the signature of a semiring and none of its
// laws: zero is no identity of it, and it does not commute.  The laws
// are evaluated at zero() and one(), so the verify helpers, which are
// constrained on Semiring, have no candidate for it.

#include <foundation/algebra/Lattice.h>

namespace fa = ::foundation::algebra;
using Subtraction = fa::detail::lattice_self_test::SubtractionSemiring;

static_assert(fa::verify_semiring_axioms_at<Subtraction>(0, 1, 1));

int main() { return 0; }
