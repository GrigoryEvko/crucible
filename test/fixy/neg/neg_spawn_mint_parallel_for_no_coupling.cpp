// mint_parallel_for takes the coupling of its shards as its first template
// argument, with no default.  Its count of threads changes with the budget
// and with the caches of the host, and two shards that wait on each other
// never end on one thread.  So a loop that does not state whether its shards
// wait on each other is refused.

#include <fixy/concurrent/Spawn.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct RegionWhole {
    using permission_row = ::foundation::effects::Row<>;
};
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
}  // namespace

// The call is refused at its template arguments, so the region holds no
// element and the fixture runs nothing.
int main() {
    BgCtx ctx{eff::testing::bg()};
    auto region =
        fixy::mint_owned_region(static_cast<int*>(nullptr), std::size_t{0}, perm::mint_permission_root<RegionWhole>());
    [[maybe_unused]] auto whole = fixy::spawn::mint_parallel_for<2>(
        ctx, fixy::concurrent::WorkBudget{}, std::move(region), [](auto& shard) noexcept { (void)shard; });
    return 0;
}
