// A clock that exists cannot be edited in place.  Lowering a slot would
// make a later event look earlier, so the slots are private and the
// lattice operations are the only writers.

#include <foundation/algebra/lattices/HappensBefore.h>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    using HB = fl::HappensBeforeLattice<2>;
    HB::element_type clock{{4, 5}};
    clock.clock_[0] = 0;
    return static_cast<int>(clock[0]);
}
