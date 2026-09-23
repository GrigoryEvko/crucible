// Two counter axes are the same lattice under two tags, and the tag is
// what keeps them apart.  The old types converted to std::uint64_t
// implicitly, so comparing an epoch with a generation compiled through
// the built-in comparison.  The element types here have no conversion,
// so the comparison has no candidate.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    fl::Epoch const epoch{3};
    fl::Generation const generation{3};
    return epoch == generation ? 0 : 1;
}
