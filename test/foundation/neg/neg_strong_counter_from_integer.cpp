// A count is not stated by an integer.  Its only constructor that takes
// one is private, so an epoch of any number is reached by steps from
// genesis or read from an image, and never written down.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    fl::Epoch const epoch{3};
    return static_cast<int>(epoch.raw());
}
