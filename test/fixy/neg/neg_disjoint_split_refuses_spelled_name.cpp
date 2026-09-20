// A caller who could spell the name of a split could write a receipt
// for a split it did not perform, and hand it to recombine beside
// somebody else's shards.  The name sits after a pack that absorbs
// whatever a caller writes, so a second template argument lands in the
// pack and the assertion fires.

#include <fixy/OwnedRegion.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <utility>

namespace {
struct Buffer {
    using permission_row = ::foundation::effects::Row<>;
};

// An empty class is a well formed brand, so nothing but the pack stops
// this from becoming the name of the split.
struct HeldName {};
}  // namespace

int main() {
    int storage[4] = {};
    auto region =
        ::fixy::mint_owned_region(storage, std::size_t{4}, ::foundation::permissions::mint_permission_root<Buffer>());
    auto parts = ::fixy::mint_split<2, HeldName>(std::move(region));
    (void)parts;
    return 0;
}
