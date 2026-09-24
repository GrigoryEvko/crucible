// A receive whose join already holds the largest count in the receiver's
// slot cannot count the receive: the step would wrap.  causal_merge hands
// the join to successor_at, and the overflow guard there fires.

#include <foundation/algebra/lattices/HappensBefore.h>

namespace fl = ::foundation::algebra::lattices;
using HB = fl::HappensBeforeLattice<2>;

constexpr HB::element_type local = HB::bottom();
constexpr HB::element_type received = HB::top();
static_assert(HB::causal_merge(local, received, 0)[1] == 3);

int main() { return 0; }
