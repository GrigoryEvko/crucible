// A slot counts events, so make_clock takes unsigned counts only.  A
// negative argument converted to the count would become the largest
// count, and the clock would claim to follow every other clock.

#include <foundation/algebra/lattices/HappensBefore.h>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    using HB = fl::HappensBeforeLattice<2>;
    auto const clock = fl::make_clock<HB>(-1, 0);
    return static_cast<int>(clock[1]);
}
