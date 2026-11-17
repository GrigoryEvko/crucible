// A new-expression gives a share guard storage that no scope ends.  A
// leaked guard keeps its share out, so the pool never upgrades and aborts
// when it ends.  SharedPermissionGuard deletes its class-scope operator
// new, so the expression is refused.

#include <foundation/permissions/SharedPermission.h>

#include <type_traits>
#include <utility>

namespace fp = ::foundation::permissions;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    auto root = fp::mint_permission_root<Region>();
    fp::SharedPermissionPool pool{std::move(root)};
    const ::foundation::effects::ExecCtx<::foundation::effects::Test,
                                         ::foundation::effects::Row<::foundation::effects::Effect::Nondet>>
        pool_ctx{::foundation::effects::testing::test()};
    auto share = pool.lend(pool_ctx).expect("an idle pool lends a share");
    using Guard = decltype(share);
    auto* leaked = new Guard(std::move(share));
    (void)leaked;
    return 0;
}
