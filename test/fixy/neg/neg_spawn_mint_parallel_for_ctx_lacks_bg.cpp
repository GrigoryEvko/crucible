// The background capability is demanded even when N is one and no thread
// is spawned, so the contract does not change shape with N.  A foreground
// context is refused.

#include <fixy/concurrent/Spawn.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <cstddef>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct RegionWhole {
    using permission_row = ::foundation::effects::Row<>;
};
using FgCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;
}  // namespace

int main() {
    FgCtx ctx{eff::testing::test()};
    test_owned_region::UncheckedArena arena{};
    auto region = fixy::mint_owned_region<int>(eff::testing::test().alloc, arena, std::size_t{16},
                                               perm::mint_permission_root<RegionWhole>());
    [[maybe_unused]] auto whole = fixy::spawn::mint_parallel_for<fixy::spawn::BodyCoupling::Independent, 2>(
        ctx, fixy::concurrent::WorkBudget{}, std::move(region), fixy::spawn::reads(),
        [](auto shard, auto) noexcept { (void)shard; });
    return 0;
}
