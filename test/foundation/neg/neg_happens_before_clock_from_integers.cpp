// A clock is not stated by integers.  Its constructor from an array of
// slots is private, so a clock claims only a history of events that its
// own doors recorded, or one read from an image.

#include <foundation/algebra/lattices/HappensBefore.h>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    using HB = fl::HappensBeforeLattice<2>;
    HB::element_type const clock{{1, 2}};
    return static_cast<int>(clock[0]);
}
