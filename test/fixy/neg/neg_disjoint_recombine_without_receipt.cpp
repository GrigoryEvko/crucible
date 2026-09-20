// Shards alone are not authority to rebuild a whole.  recombine took a
// tuple and nothing else before the receipt existed, so a caller who
// held the shards could assemble a tuple and get a region back.  The
// one-argument call is now no call at all.

#include <fixy/OwnedRegion.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <tuple>
#include <utility>

namespace {
struct Buffer {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    int storage[4] = {};
    auto region =
        ::fixy::mint_owned_region(storage, std::size_t{4}, ::foundation::permissions::mint_permission_root<Buffer>());
    using Region = decltype(region);
    auto parts = ::fixy::mint_split<2>(std::move(region));
    auto whole = Region::recombine(std::move(parts.shards));
    (void)whole;
    return 0;
}
