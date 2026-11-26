// A body of the frame door with_mut of a region opens the door of the same
// region again.  The two bodies would hold two ViewMuts of one run, and each
// could write it beside the other.  The body reaches the region through a
// capture by reference, and a region owns no storage, so the capture is a
// borrow slot: the store twin of the outer door refuses the body
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).  The twin of a
// named region refuses a door of a named region
// (neg_owned_region_with_mut_of_named).

#include <fixy/OwnedRegion.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <utility>

namespace {
struct RegionWhole {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    test_owned_region::UncheckedArena arena{};
    auto region = ::fixy::mint_owned_region<int>(::foundation::effects::testing::test().alloc, arena, 0uz,
                                                 ::foundation::permissions::mint_permission_root<RegionWhole>());
    auto back = ::fixy::with_mut(std::move(region), [&region](auto outer) noexcept {
        auto again = ::fixy::with_mut(std::move(region), [](auto inner) noexcept { static_cast<void>(inner.size()); });
        ::foundation::linear::drop(std::move(again).release());
        static_cast<void>(outer.size());
    });
    ::foundation::linear::drop(std::move(back).release());
    return 0;
}
