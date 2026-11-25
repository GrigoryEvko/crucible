// Negative compile test.  This file must fail to compile.
//
// A parallel-for over zero shards visits nothing, so the door of
// mint_parallel_for refuses N of zero.  Without that check the call
// passes the door and fails later, inside the split.  The regexes below
// name the door and its failed conjunct, so they tell the two apart.
//
// Expected diagnostic: mint_parallel_for has no viable candidate, and the
// note names the (N > 0) conjunct of CtxFitsParallelFor.

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
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
}  // namespace

int main() {
    BgCtx ctx{eff::testing::bg()};
    test_owned_region::UncheckedArena arena{};
    auto region = fixy::mint_owned_region<int>(eff::testing::test().alloc, arena, std::size_t{16},
                                               perm::mint_permission_root<RegionWhole>());
    [[maybe_unused]] auto whole = fixy::spawn::mint_parallel_for<fixy::spawn::BodyCoupling::Independent, 0>(
        ctx, fixy::concurrent::WorkBudget{}, std::move(region), fixy::spawn::reads(),
        [](auto shard, auto) noexcept { (void)shard; });
    return 0;
}
