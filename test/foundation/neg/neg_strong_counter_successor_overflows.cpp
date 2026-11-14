// The largest count has no successor.  The step would wrap to zero, and a
// newer count would then read as the oldest one.  The guard is a
// CRUCIBLE_PRE in the body of successor, so it fires during constant
// evaluation.  Each axis runs the same body.  A use axis names its largest
// count as its top, and a version axis has no top.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

namespace fl = ::foundation::algebra::lattices;

static_assert(fl::PeakBytesLattice::successor(fl::PeakBytesLattice::top()).raw() == 0);

int main() { return 0; }
