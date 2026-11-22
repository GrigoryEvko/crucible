// A body of mint_parallel_for takes the region of its shard by reference.  A
// body that held the region could open a second door of its run beside the
// ViewMut of its frame, through a helper or a closure, and two writers would
// name one run.  The runner of mint_parallel_for is the frame door of each
// shard (fixy/concurrent/Spawn.h): a body gets the ViewMut of the elements of
// its shard and the place of the shard, and no body names a region.  So the
// gate of the mint refuses a body that asks for the region.

#include <fixy/concurrent/Spawn.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct RegionWhole {
    using permission_row = ::foundation::effects::Row<>;
};
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

// The body of the old form: the shard as a region, by reference.
struct RegionBody {
    template <class Tag, class Brand, class Place>
    void operator()(fixy::OwnedRegion<int, Tag, Brand>& shard, Place) const noexcept {
        static_cast<void>(shard);
    }
};
}  // namespace

int main() {
    BgCtx ctx{eff::testing::bg()};
    test_owned_region::UncheckedArena arena{};
    auto region =
        fixy::mint_owned_region<int>(eff::testing::test().alloc, arena, 0uz, perm::mint_permission_root<RegionWhole>());
    [[maybe_unused]] auto whole = fixy::spawn::mint_parallel_for<fixy::spawn::BodyCoupling::Independent, 2>(
        ctx, fixy::concurrent::WorkBudget{}, std::move(region), RegionBody{});
    return 0;
}
