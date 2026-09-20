// The background capability is demanded even when N is one and no thread
// is spawned, so the contract does not change shape with N.  A foreground
// context is refused.

#include <fixy/os/Spawn.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct RegionWhole {};
using FgCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;
std::array<int, 16> storage{};
}  // namespace

int main() {
    FgCtx ctx{eff::testing::test()};
    auto region = fixy::OwnedRegion<int, RegionWhole>::wrap(storage.data(), storage.size(),
                                                            perm::mint_permission_root<RegionWhole>());
    [[maybe_unused]] auto whole =
        fixy::spawn::mint_parallel_for<2>(ctx, std::move(region), [](auto& shard) noexcept { (void)shard; });
    return 0;
}
