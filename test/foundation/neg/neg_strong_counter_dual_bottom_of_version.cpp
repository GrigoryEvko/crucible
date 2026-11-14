// The bottom of the dual of a version axis is the top of the axis: the
// newest version, the strongest claim of the dual order.  The axis has no
// top, so the dual has no bottom, and the newest epoch has no door
// through the dual either.

#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>

namespace fl = ::foundation::algebra::lattices;

int main() {
    fl::Epoch const newest = fl::DualLattice<fl::EpochLattice>::bottom();
    return static_cast<int>(newest.raw() & 1u);
}
