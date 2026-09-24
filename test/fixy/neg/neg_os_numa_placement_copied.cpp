// A placement proof owns its region, so it has no copy.  A copy would give
// two owners of one mapping, and the second destructor would unmap an
// address that the first one had already unmapped.
//
// The copying function is never called.  The error is at the copy, so the
// fixture needs no binding.

#include <fixy/os/NumaPlacement.h>

namespace {
struct Region final {};
using Placement = fixy::NumaPlacement<Region, fixy::mmap::prot::WriteCopy>;

[[maybe_unused]] void duplicate(Placement const& earned) {
    Placement second{earned};
    (void)second;
}
}  // namespace

int main() { return 0; }
