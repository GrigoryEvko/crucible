// Negative-compile test.  This file must fail to compile.
//
// The body of a region scope door takes the region by value and gives it
// back.  A body that drops the region and gives a count has no region to give
// back, so it does not fit the door, and the door ends no token that the body
// lost.
//
// Expected diagnostic: with_region has no candidate for the body.

#include <fixy/OwnedRegion.h>
#include <foundation/Linear.h>
#include <foundation/core/Array.h>
#include <foundation/effects/Ctx.h>

#include <utility>

namespace {

struct Slots {
    using permission_row = ::foundation::effects::Row<>;
    using scope_door = ::fixy::RegionScopeDoor;
};

}  // namespace

int main() {
    auto storage = ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 0);
    auto kept = ::fixy::with_region<Slots>(std::move(storage), [](auto region) noexcept {
        ::foundation::linear::drop(std::move(region));
        return 1;
    });
    static_cast<void>(kept);
    return 0;
}
