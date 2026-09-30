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
    namespace fp = ::foundation::permissions;
    int storage_a[4] = {};
    int storage_b[4] = {};
    using Region = ::fixy::OwnedRegion<int, Buffer>;

    // Both regions are built on the erased identity, so their shards are
    // one type and the mixed tuple below is well formed.  The receipt is
    // what refuses.
    Region erased_first =
        Region::wrap(storage_a, std::size_t{4}, fp::permission_erase_brand(fp::mint_permission_root<Buffer>()));
    Region erased_second =
        Region::wrap(storage_b, std::size_t{4}, fp::permission_erase_brand(fp::mint_permission_root<Buffer>()));
    auto parts_first = ::fixy::mint_split<2>(std::move(erased_first));
    auto parts_second = ::fixy::mint_split<2>(std::move(erased_second));

    auto whole = Region::recombine(std::move(parts_first.witness), std::move(parts_second.shards));
    (void)whole;
    return 0;
}
