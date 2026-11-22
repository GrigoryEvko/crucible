// Negative-compile test.  This file must fail to compile.
//
// The body of a region scope door gives its value out of the frame.  A value
// that names the scope brand of the frame would carry a proof about the frame
// out of it, so the body does not fit the door.
//
// Expected diagnostic: with_region has no candidate for the body.

#include <fixy/OwnedRegion.h>
#include <foundation/core/Array.h>
#include <foundation/core/Record.h>
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
        using Brand = typename decltype(region)::brand_type;
        return ::foundation::core::Record{Brand{}, std::move(region)};
    });
    static_cast<void>(kept);
    return 0;
}
