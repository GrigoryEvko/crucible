// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A parallel-for over zero shards visits nothing, so the door of
// mint_parallel_for refuses N of zero.  Without that check the call
// passes the door and fails later, inside the split.  The regexes below
// name the door and its failed conjunct, so they tell the two apart.
//
// Expected diagnostic: mint_parallel_for has no viable candidate, and the
// note names the (N > 0) conjunct of CtxFitsParallelFor.

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
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
std::array<int, 16> storage{};
}  // namespace

int main() {
    BgCtx ctx{eff::testing::bg()};
    auto region = fixy::mint_owned_region(storage.data(), storage.size(), perm::mint_permission_root<RegionWhole>());
    [[maybe_unused]] auto whole =
        fixy::spawn::mint_parallel_for<0>(ctx, std::move(region), [](auto& shard) noexcept { (void)shard; });
    return 0;
}
