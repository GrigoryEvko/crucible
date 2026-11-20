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

int storage_a[4] = {};
int storage_b[4] = {};

// The arguments of the root mint are concrete, so each instantiation of the
// template shares one brand (foundation/Brand.h).  The two regions below are
// then one type, and the brand does not refuse the mixed rebuild.  The
// receipt is what refuses.
template <auto& Storage>
auto region_at_one_site() {
    return ::fixy::mint_owned_region(Storage, std::size_t{4},
                                     ::foundation::permissions::mint_permission_root<Buffer>());
}
}  // namespace

int main() {
    auto first = region_at_one_site<storage_a>();
    auto second = region_at_one_site<storage_b>();
    using Region = decltype(first);
    auto parts_first = ::fixy::mint_split<2>(std::move(first));
    auto parts_second = ::fixy::mint_split<2>(std::move(second));

    auto whole = Region::recombine(std::move(parts_first.witness), std::move(parts_second.shards));
    (void)whole;
    return 0;
}
