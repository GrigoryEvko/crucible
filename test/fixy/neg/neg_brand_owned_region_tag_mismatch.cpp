// The region mint reads its tag off the permission it consumes, so a
// region cannot be minted under one tag with a permission for another:
// the tag named at the call and the tag on the permission fail to
// unify, and no region is made.

#include <fixy/OwnedRegion.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <cstddef>

namespace {
struct Buffer {
    using permission_row = ::foundation::effects::Row<>;
};
struct OtherBuffer {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    test_owned_region::UncheckedArena arena{};
    auto region = ::fixy::mint_owned_region<int, test_owned_region::UncheckedArena, Buffer>(
        ::foundation::effects::testing::test().alloc, arena, std::size_t{4},
        ::foundation::permissions::mint_permission_root<OtherBuffer>());
    (void)region;
    return 0;
}
