// with_mut of a named region.  The region would stay with the caller while the
// body runs, so the caller could write it beside the ViewMut of the body.  The
// door takes the region by move, and the twin of a named region refuses it.

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
    ::fixy::with_mut(region, [](auto inner) noexcept { static_cast<void>(inner.size()); });
    ::foundation::linear::drop(std::move(region).release());
    return 0;
}
