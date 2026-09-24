// A clock is not rebuilt from the bytes of an array.  std::bit_cast needs a
// trivially copyable type, and the user-provided assignments of a clock
// make it not one.

#include <foundation/algebra/lattices/HappensBefore.h>

#include <array>
#include <bit>
#include <cstdint>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    using HB = fl::HappensBeforeLattice<2>;
    auto const clock = std::bit_cast<HB::element_type>(std::array<std::uint64_t, 2>{7, 7});
    return static_cast<int>(clock[0]);
}
