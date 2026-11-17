// A pool that ends while a share is out aborts with a named diagnostic.
//
// Each guard keeps a pointer into its pool, and the release of the guard
// writes the pool's count.  Without the check in the pool's destructor,
// the release below writes freed memory.  The attack runs in a child
// process through expect_dies of the Check family, and the test passes only
// when the child ends by SIGABRT with that diagnostic.

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/SharedPermission.h>

#include <memory>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

void end_pool_while_shared() {
    namespace fp = ::foundation::permissions;
    auto root = fp::mint_permission_root<Region>();
    using Brand = decltype(root)::brand_type;
    auto pool = std::make_unique<fp::SharedPermissionPool<Region, Brand>>(std::move(root));
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::foundation::core::Option<fp::SharedPermissionGuard<Region, Brand>> const share = pool->lend(test);
    // The pool ends here, while the share is out.
    pool.reset();
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(test, end_pool_while_shared,
                               ::foundation::core::TextView{"a SharedPermissionPool ended while shares were out"});
    ::fixy::check::pass(test, "test_share_pool_outlived_aborts: refused\n");
    return 0;
}
