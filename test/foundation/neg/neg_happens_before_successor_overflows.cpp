// A slot at the largest value cannot advance: the increment would wrap
// to zero, and the successor would then sit below its own input.  The
// guard is a CRUCIBLE_PRE in the body of successor_at, so it fires
// during constant evaluation, where a native pre() clause is skipped.

#include <foundation/algebra/lattices/HappensBefore.h>

#include <limits>

namespace fl = ::foundation::algebra::lattices;
using HB = fl::HappensBeforeLattice<2>;

constexpr HB::element_type saturated{{std::numeric_limits<std::uint64_t>::max(), 0}};
static_assert(HB::successor_at(saturated, 0)[1] == 0);

int main() { return 0; }
