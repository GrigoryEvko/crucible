// A lattice whose bottom() sits above its top() satisfies every law
// between elements.  The law that bottom() is below top() refuses it, so
// Graded, which asks for a lattice, refuses a grade drawn from it.

#include <foundation/algebra/Graded.h>

namespace fa = ::foundation::algebra;
using Exchanged = fa::detail::ExchangedBounds;

using Carrier = fa::Graded<fa::ModalityKind::Absolute, Exchanged, int>;

int main() { return 0; }
