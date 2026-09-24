// A receipt written by one split does not authorize the rebuild of
// another.  Two regions of one tag are split at two sites, and the
// receipt of the first is offered with the shards of the second.  The
// name of the split site is in the receipt's type, so the two receipts
// are two types and neither converts to the other.

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
    int storage_a[4] = {};
    int storage_b[4] = {};
    auto first =
        ::fixy::mint_owned_region(storage_a, std::size_t{4}, ::foundation::permissions::mint_permission_root<Buffer>());
    auto second =
        ::fixy::mint_owned_region(storage_b, std::size_t{4}, ::foundation::permissions::mint_permission_root<Buffer>());
    using Region = ::fixy::OwnedRegion<int, Buffer>;

    // Both regions erase to the unbranded spelling, so their shards are
    // one type and the mixed tuple below is well formed.  The receipt is
    // what refuses.
    Region erased_first = std::move(first);
    Region erased_second = std::move(second);
    auto parts_first = ::fixy::mint_split<2>(std::move(erased_first));
    auto parts_second = ::fixy::mint_split<2>(std::move(erased_second));

    auto whole = Region::recombine(std::move(parts_first.witness), std::move(parts_second.shards));
    (void)whole;
    return 0;
}
