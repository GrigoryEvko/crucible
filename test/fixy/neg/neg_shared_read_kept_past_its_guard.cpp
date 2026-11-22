// A read kept in an Option that outlives the guard of its share.  After the
// scope of the guard ends, the pool can give the exclusive back, and a write
// through the region would change the elements under the kept read.  A read
// does not copy and does not move, so no Option holds one.

#include <fixy/SharedRegion.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <utility>

namespace eff = ::foundation::effects;

namespace {
struct Cache {
    using permission_row = eff::Row<>;
};
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Nondet>>;
}  // namespace

int main() {
    BgCtx const ctx{eff::testing::bg()};
    test_owned_region::UncheckedArena arena{};
    ::fixy::SharedRegion shared{::fixy::mint_owned_region<int>(
        eff::testing::test().alloc, arena, 0uz, ::foundation::permissions::mint_permission_root<Cache>())};
    // The lambda takes a share and a read, and gives the read back in an
    // Option, which outlives the guard, a local of the lambda.
    auto const keep_past_the_guard = [&ctx, &shared]() noexcept {
        auto guard = shared.lend(ctx).expect("an idle region lends a share");
        auto read = ::fixy::mint_shared_read(ctx, guard, shared);
        return ::foundation::core::Option<decltype(read)>::some(read);
    };
    static_cast<void>(keep_past_the_guard);
    return 0;
}
