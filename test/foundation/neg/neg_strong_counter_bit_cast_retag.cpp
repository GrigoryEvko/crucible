// A count is not rebuilt from the bytes of a count of another axis.
// std::bit_cast needs a trivially copyable type, and the user-provided
// assignments of a count make it not one, while its copy stays trivial.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

#include <bit>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    auto const generation = std::bit_cast<fl::Generation>(fl::EpochLattice::bottom());
    return static_cast<int>(generation.raw());
}
