// A counter that exists cannot be edited in place.  A write to the count
// would lower an epoch that other code already compared against, so the
// count is private, and only the explicit constructor and successor()
// produce a counter.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    fl::Epoch epoch{9};
    epoch.value_ = 1;
    return static_cast<int>(epoch.raw());
}
