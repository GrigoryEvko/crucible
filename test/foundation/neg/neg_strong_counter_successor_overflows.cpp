// The largest count has no successor.  The step would wrap to zero, and a
// newer epoch would then read as the oldest one.  The guard is a
// CRUCIBLE_PRE in the body of successor, so it fires during constant
// evaluation.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

namespace fl = ::foundation::algebra::lattices;

static_assert(fl::EpochLattice::successor(fl::EpochLattice::top()).raw() == 0);

int main() { return 0; }
