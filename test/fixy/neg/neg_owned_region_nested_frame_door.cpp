// A body of the frame door with_mut of a region opens the door of the same
// region again.  The two bodies would hold two ViewMuts of one run, and each
// could write it beside the other.  The door takes the region by move, so the
// caller holds no name for the region while the body runs, and the twin of a
// named region refuses the inner door.

#include <fixy/OwnedRegion.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct RegionWhole {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    auto region =
        ::fixy::mint_owned_region<int>(nullptr, 0uz, ::foundation::permissions::mint_permission_root<RegionWhole>());
    auto back = ::fixy::with_mut(std::move(region), [&region](auto outer) noexcept {
        ::fixy::with_mut(region, [](auto inner) noexcept { static_cast<void>(inner.size()); });
        static_cast<void>(outer.size());
    });
    ::foundation::linear::drop(std::move(back).release());
    return 0;
}
