// A version axis has no top.  On that axis a higher count is a newer
// version, so a top would be the newest epoch with no successor step, and
// it would win each join.  The top exists on a use axis only.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

namespace fl = ::foundation::algebra::lattices;

int main() {
    fl::Epoch const newest = fl::EpochLattice::top();
    return static_cast<int>(newest.raw() & 1u);
}
