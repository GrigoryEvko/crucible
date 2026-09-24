// std::bit_cast builds a trivially copyable type from bytes, with no
// constructor.  A placement proof has a user-provided move constructor and
// owns a region that its destructor unmaps, so it is not trivially
// copyable, and bit_cast refuses it at its constraint.

#include <fixy/os/NumaPlacement.h>

#include <bit>

namespace {
struct Region final {};
using Placement = fixy::NumaPlacement<Region, fixy::mmap::prot::WriteCopy>;
struct Bytes {
    unsigned char raw[sizeof(Placement)];
};
}  // namespace

int main() {
    auto forged = std::bit_cast<Placement>(Bytes{});
    (void)forged;
    return 0;
}
