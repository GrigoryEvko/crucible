// A vector clock has no top.  A top would claim the longest history with
// no recorded event, and it would win each join and each receive.

#include <foundation/algebra/lattices/HappensBefore.h>

namespace fl = ::foundation::algebra::lattices;
using HB = fl::HappensBeforeLattice<2>;

int main() {
    HB::element_type const longest = HB::top();
    return static_cast<int>(longest[0] & 1u);
}
