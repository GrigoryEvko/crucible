// A receive whose join already holds the largest count in the receiver's
// slot cannot count the receive: the step would wrap.  causal_merge hands
// the join to successor_at, and the overflow guard there fires.

#include <foundation/algebra/lattices/HappensBefore.h>

#include <limits>

namespace fl = ::foundation::algebra::lattices;
using HB = fl::HappensBeforeLattice<2>;

constexpr HB::element_type local{{1, 0}};
constexpr HB::element_type received{{std::numeric_limits<std::uint64_t>::max(), 3}};
static_assert(HB::causal_merge(local, received, 0)[1] == 3);

int main() { return 0; }
