// A region that dies at the end of the statement leaves the read pointing
// into storage that is gone.  The guard here is live and names the same
// brand as the temporary region, because one function makes both regions
// and one call site mints one brand.  Only the deleted rvalue twin refuses
// the call, because the lifetime annotation alone carries no weight on
// this compiler.

#include <fixy/SharedRegion.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <cstddef>
#include <utility>

namespace {
struct Cache {
    using permission_row = ::foundation::effects::Row<>;
};
using BgCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Bg,
    ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Nondet>>;

// Each call returns a region of the same brand.
auto region_over(test_owned_region::UncheckedArena& arena) {
    return ::fixy::SharedRegion{
        ::fixy::mint_owned_region<int>(::foundation::effects::testing::test().alloc, arena, std::size_t{2},
                                       ::foundation::permissions::mint_permission_root<Cache>())};
}
}  // namespace

int main() {
    test_owned_region::UncheckedArena arena{};
    BgCtx ctx{::foundation::effects::testing::bg()};

    auto shared = region_over(arena);
    auto guard = shared.lend(ctx).expect("an idle region lends a share");
    auto read = ::fixy::mint_shared_read(ctx, guard, region_over(arena));
    (void)read;
    return 0;
}
