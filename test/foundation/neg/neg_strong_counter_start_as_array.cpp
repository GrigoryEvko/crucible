// A count is not started over bytes.  A count is implicit-lifetime, because
// its copy is trivial, so the class carries no_start_over_bytes, and the
// checked lifetime start refuses it.

#include <foundation/Lifetime.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>

int main() {
    namespace fl = ::foundation::algebra::lattices;
    alignas(8) unsigned char bytes[8]{};
    auto const counts = ::foundation::lifetime::start_as_array<fl::Epoch>(bytes, 1);
    return static_cast<int>(counts[0].raw());
}
