// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_parallel_for_with_thread_options starts worker threads as
// mint_parallel_for does, so its first context must own Bg.  A test
// context owns Test and no Bg.  It is the authority of the options and not
// of the threads, so the door refuses it in the place of the background
// context and names the gate of mint_parallel_for.

#include <fixy/concurrent/Spawn.h>
#include <foundation/permissions/Permission.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct RegionWhole {
    using permission_row = ::foundation::effects::Row<>;
};
using TestCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;
}  // namespace

int main() {
    const TestCtx test{eff::testing::test()};
    auto region = fixy::mint_owned_region<int>(nullptr, 0, perm::mint_permission_root<RegionWhole>());
    [[maybe_unused]] auto whole = fixy::spawn::mint_parallel_for_with_thread_options<2>(
        test, test, fixy::concurrent::WorkBudget{}, std::move(region), [](auto& shard) noexcept { (void)shard; },
        fixy::thread::ThreadOptions{});
    return 0;
}
