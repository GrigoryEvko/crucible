// A borrow of a region is a View of the elements that the region holds.  A
// temporary region gives its permission back at the end of the full
// expression, so a View of it outlives the claim on its elements.  The
// deleted rvalue twin of mint_borrowed refuses it.

#include <fixy/OwnedRegion.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <cstddef>

namespace {
struct Cache {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    test_owned_region::UncheckedArena arena{};
    [[maybe_unused]] auto borrow = ::fixy::mint_borrowed(
        ::fixy::mint_owned_region<int>(::foundation::effects::testing::test().alloc, arena, std::size_t{2},
                                       ::foundation::permissions::mint_permission_root<Cache>()));
    return 0;
}
