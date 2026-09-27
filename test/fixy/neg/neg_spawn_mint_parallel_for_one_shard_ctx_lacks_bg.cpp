// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The background capability is demanded even when N is one and no thread
// is started, so the contract does not change shape with N.  With one
// shard the body runs inline and the fan-out runner is never reached, so
// the check on the door of mint_parallel_for is the only one that refuses
// a context without Bg.
//
// Expected diagnostic: mint_parallel_for has no viable candidate, and the
// note names the Bg check of CtxFitsParallelFor.

#include <fixy/os/Spawn.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct RegionWhole {
    using permission_row = ::foundation::effects::Row<>;
};
using TestCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;
std::array<int, 16> storage{};
}  // namespace

int main() {
    TestCtx ctx{eff::testing::test()};
    auto region = fixy::mint_owned_region(storage.data(), storage.size(), perm::mint_permission_root<RegionWhole>());
    [[maybe_unused]] auto whole =
        fixy::spawn::mint_parallel_for<1>(ctx, fixy::concurrent::WorkBudget{}, std::move(region),
                                          [](auto& shard) noexcept { (void)shard; });
    return 0;
}
