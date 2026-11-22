// Negative compile test.  This file must fail to compile.
//
// Each worker of a parallel-for touches its shard of the region under the
// context of the call, so the context must admit the row of the region.
// The region here does IO, and the background context of the call admits
// no IO.  Without the row check on the door, the call passes the door and
// fails later, inside the split.  The regexes below name the door and its
// failed conjunct, so they tell the two apart.
//
// Expected diagnostic: mint_parallel_for has no viable candidate, and the
// note names the CtxAdmitsPermission conjunct of CtxFitsParallelFor.

#include <fixy/concurrent/Spawn.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <cstddef>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct IoRegion {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
using TestCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    TestCtx test_ctx{eff::testing::test()};
    BgCtx ctx{eff::testing::bg()};
    test_owned_region::UncheckedArena arena{};
    auto region = fixy::mint_owned_region<int>(eff::testing::test().alloc, arena, std::size_t{16},
                                               perm::mint_permission_root<IoRegion>(test_ctx));
    [[maybe_unused]] auto whole = fixy::spawn::mint_parallel_for<fixy::spawn::BodyCoupling::Independent, 2>(
        ctx, fixy::concurrent::WorkBudget{}, std::move(region), [](auto shard, auto) noexcept { (void)shard; });
    return 0;
}
