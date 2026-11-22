// Negative-compile test. This file must fail to compile.
//
// One root site that runs two times gives two roots of one brand.  A pool
// built from the first root gives its exclusive out as a lease, and the root
// of the second run goes to the deposit of the same pool.  The deposit takes
// only a lease of the pool (the lease of foundation/permissions/
// SharedPermission.h), so the second root has no door into the pool, and no
// share of the pool can live beside the lease of the first root.
//
// Expected diagnostic: the deposit cannot convert the root to a lease.

#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/SharedPermission.h>

#include <utility>

namespace {
namespace eff = ::foundation::effects;
using PoolCtxOfFixture = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Nondet>>;

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

// The one site of the root.  Each call runs the same site, so each root has
// the same brand.
[[nodiscard]] auto mint_region_root() noexcept { return ::foundation::permissions::mint_permission_root<Region>(); }
}  // namespace

int main() {
    namespace perm = ::foundation::permissions;
    PoolCtxOfFixture const ctx{eff::testing::bg()};
    perm::SharedPermissionPool pool{mint_region_root()};
    auto lease = pool.try_upgrade(ctx).expect("an idle pool gives its exclusive");
    pool.deposit_exclusive(mint_region_root());
    ::foundation::linear::drop(std::move(lease));
    return 0;
}
