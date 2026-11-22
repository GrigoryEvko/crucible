// One split authorizes one rebuild.  A copied receipt would authorize a
// second, so the copy constructor is deleted with its reason.

#include <fixy/OwnedRegion.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <cstddef>
#include <utility>

namespace {
struct Buffer {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    test_owned_region::UncheckedArena arena{};
    auto region = ::fixy::mint_owned_region<int>(::foundation::effects::testing::test().alloc, arena, std::size_t{4},
                                                 ::foundation::permissions::mint_permission_root<Buffer>());
    auto parts = ::fixy::mint_split<2>(std::move(region));
    auto second_receipt = parts.witness;
    (void)second_receipt;
    return 0;
}
