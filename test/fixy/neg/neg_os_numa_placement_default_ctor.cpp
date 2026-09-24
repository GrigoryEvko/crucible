// A placement proof has no default constructor.  A default proof would
// claim a binding to a node that no call to mbind made, and a consumer
// that reads node() would place work next to memory that is on another
// node.  The only door is fixy::numa::mint_numa_placement.

#include <fixy/os/NumaPlacement.h>

namespace {
struct Region final {};
using Placement = fixy::NumaPlacement<Region, fixy::mmap::prot::WriteCopy>;
}  // namespace

int main() {
    Placement forged{};
    (void)forged;
    return 0;
}
