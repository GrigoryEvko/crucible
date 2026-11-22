// A receipt written by one split does not authorize the rebuild of
// another.  Two regions of one tag are split at two sites, and the
// receipt of the first is offered with the shards of the second.  The
// name of the split site is in the receipt's type, so the two receipts
// are two types and neither converts to the other.

#include <fixy/OwnedRegion.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <cstddef>
#include <tuple>
#include <utility>

namespace {
struct Buffer {
    using permission_row = ::foundation::effects::Row<>;
};

// One function is one site of the root mint, so each call shares one brand
// (foundation/Brand.h).  The two regions below are then one type, and the
// brand does not refuse the mixed rebuild.  The receipt is what refuses.
auto region_at_one_site(test_owned_region::UncheckedArena& arena) {
    return ::fixy::mint_owned_region<int>(::foundation::effects::testing::test().alloc, arena, std::size_t{4},
                                          ::foundation::permissions::mint_permission_root<Buffer>());
}
}  // namespace

int main() {
    test_owned_region::UncheckedArena arena{};
    auto first = region_at_one_site(arena);
    auto second = region_at_one_site(arena);
    using Region = decltype(first);
    auto parts_first = ::fixy::mint_split<2>(std::move(first));
    auto parts_second = ::fixy::mint_split<2>(std::move(second));

    auto whole = Region::recombine(std::move(parts_first.witness), std::move(parts_second.shards));
    (void)whole;
    return 0;
}
