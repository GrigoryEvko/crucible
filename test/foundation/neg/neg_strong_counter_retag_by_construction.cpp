// A generation cannot be built from an epoch.  The explicit constructor
// takes a count, and neither axis converts to a count or to the other
// axis, so the retag has no constructor to call.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    fl::Epoch const epoch{9};
    fl::Generation const generation{epoch};
    return static_cast<int>(generation.raw());
}
