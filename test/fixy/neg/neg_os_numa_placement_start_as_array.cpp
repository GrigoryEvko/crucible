// The checked lifetime start refuses a placement proof.  No constructor of
// the proof is trivial and its destructor unmaps a region, so it is not
// an implicit-lifetime type, and start_as_array refuses it at its
// constraint.

#include <fixy/os/NumaPlacement.h>
#include <foundation/Lifetime.h>

namespace {
struct Region final {};
using Placement = fixy::NumaPlacement<Region, fixy::mmap::prot::WriteCopy>;
}  // namespace

int main() {
    alignas(Placement) unsigned char bytes[sizeof(Placement)]{};
    auto const forged = ::foundation::lifetime::start_as_array<Placement>(bytes, 1);
    return static_cast<int>(forged.size());
}
