// The arena door of a region refuses a proof type for each allocator.
// UncheckedArena puts no constraint on its element.  A region of Permission
// elements over the bytes of an allocator would give out proof objects whose
// lifetime never started.  The door requires an element that is an
// implicit-lifetime type throughout, so the region is not built.

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
struct Proofs {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    test_owned_region::UncheckedArena arena{};
    auto region = ::fixy::mint_owned_region<::foundation::permissions::Permission<Proofs>>(
        ::foundation::effects::testing::test().alloc, arena, std::size_t{2},
        ::foundation::permissions::mint_permission_root<Cache>());
    auto token = std::move(region).release();
    (void)token;
    return 0;
}
