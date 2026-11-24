// Negative-compile test.  This file must fail to compile.
//
// The body of a region scope door keeps a View in a variable that it captures
// by reference.  A View of the elements of the region would outlive the
// frame, and the caller would then write the storage that the door gives
// back through a second writer.  The store check of the door
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h) refuses the body,
// whatever the source of the View.

#include <fixy/OwnedRegion.h>
#include <foundation/core/Array.h>
#include <foundation/core/Region.h>
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
    ::foundation::core::View<int const> kept{};
    auto back = ::fixy::with_region<Slots>(std::move(storage), [&kept](auto region) noexcept {
        kept = ::foundation::core::View<int const>{};
        return region;
    });
    static_cast<void>(back);
    return static_cast<int>(kept.size());
}
