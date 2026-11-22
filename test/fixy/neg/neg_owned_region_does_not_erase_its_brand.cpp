// A region minted with a fresh brand does not erase.  An erased region is
// one type with each other region of the tag, so a shared read of it can
// pair a guard of one region with the bytes of another.  No conversion
// drops the brand of a region, so the erased region is not built.

#include <fixy/OwnedRegion.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <cstddef>
#include <utility>

namespace {
struct Cache {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    test_owned_region::UncheckedArena arena{};
    auto branded = ::fixy::mint_owned_region<int>(::foundation::effects::testing::test().alloc, arena, std::size_t{2},
                                                  ::foundation::permissions::mint_permission_root<Cache>());
    ::fixy::OwnedRegion<int, Cache> erased = std::move(branded);
    (void)erased;
    return 0;
}
