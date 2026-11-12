// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_parallel_for_with_thread_options gives the worker threads the
// options of a test, so its second context must own Test.  Only the
// testing door mints such a context.  A background context owns Bg and no
// Test, so the door refuses the call and names the gate of the test
// context.

#include <fixy/concurrent/Spawn.h>
#include <foundation/permissions/Permission.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct RegionWhole {
    using permission_row = ::foundation::effects::Row<>;
};
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;
}  // namespace

int main() {
    const BgCtx ctx{eff::testing::bg()};
    auto region = fixy::mint_owned_region<int>(nullptr, 0, perm::mint_permission_root<RegionWhole>());
    [[maybe_unused]] auto whole = fixy::spawn::mint_parallel_for_with_thread_options<2>(
        ctx, ctx, fixy::concurrent::WorkBudget{}, std::move(region), [](auto& shard) noexcept { (void)shard; },
        fixy::thread::ThreadOptions{});
    return 0;
}
