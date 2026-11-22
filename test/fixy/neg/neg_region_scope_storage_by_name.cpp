// Negative-compile test.  This file must fail to compile.
//
// A region scope door takes its storage by move, so the caller holds no name
// of the storage while the frame of the door holds it, and no second region
// covers it.  A storage that the caller keeps by name does not fit the door.
//
// Expected diagnostic: with_region has no candidate for an lvalue HeapArray.

#include <fixy/OwnedRegion.h>
#include <foundation/core/Array.h>
#include <foundation/effects/Ctx.h>

namespace {

struct Slots {
    using permission_row = ::foundation::effects::Row<>;
    using scope_door = ::fixy::RegionScopeDoor;
};

}  // namespace

int main() {
    auto storage = ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 0);
    auto kept = ::fixy::with_region<Slots>(storage, [](auto region) noexcept { return region; });
    static_cast<void>(kept);
    return 0;
}
