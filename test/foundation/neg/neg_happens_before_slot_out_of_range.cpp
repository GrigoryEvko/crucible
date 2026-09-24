// A clock of two slots has no slot two.  The reader checks its index in
// the body, so the check fires during constant evaluation.

#include <foundation/algebra/lattices/HappensBefore.h>

namespace fl = ::foundation::algebra::lattices;
using HB = fl::HappensBeforeLattice<2>;

namespace {
constexpr HB::element_type clock = HB::bottom();
static_assert(clock[2] == 0);
}  // namespace

int main() { return 0; }
