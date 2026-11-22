// A pool takes back only its own lease (THE LEASE of
// foundation/permissions/SharedPermission.h).
//
// A brand names a mint site, so two root mints through one site are two
// tokens of one type, and two pools built from them are one type.  The
// deposit takes only a lease, so a second root has no door into a pool, and a
// fixture holds that.  A lease of the first pool still fits the deposit of the
// second pool by its type.  Without the check of the identity in
// deposit_exclusive, the second pool parks the exclusive of the first one
// while it lends, and the region of the first pool then has a share and an
// exclusive at one time.  The attack runs in a child process through
// expect_dies of the Check family, and the test passes only when the child
// ends by SIGABRT with the refusal in its diagnostic.  A deposit of the own
// lease is the control.
//
// Two targets build this file.  test_share_pool_deposit_under_ignore
// builds it with the ignore semantic, where a precondition is only an
// assumption.  So the check of the identity is a test with a cold end of the
// process, and not a contract.

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/SharedPermission.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

// One mint site, so every call returns one type.
[[nodiscard]] auto mint_region() noexcept { return ::foundation::permissions::mint_permission_root<Region>(); }

// The lease of the first pool goes into the second pool, which lends.
void deposit_lease_of_another_pool() {
    namespace fp = ::foundation::permissions;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    fp::SharedPermissionPool first_pool{mint_region()};
    fp::SharedPermissionPool second_pool{mint_region()};
    auto lease = first_pool.try_upgrade(test).expect("an idle pool gives its exclusive");
    second_pool.deposit_exclusive(std::move(lease));
}

// The control: the lease goes back into its own pool, which then lends.
[[nodiscard]] bool deposit_own_lease() {
    namespace fp = ::foundation::permissions;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    fp::SharedPermissionPool pool{mint_region()};
    auto lease = pool.try_upgrade(test).expect("an idle pool gives its exclusive");
    bool const is_lease_of_pool = lease.is_lease_of(pool);
    pool.deposit_exclusive(std::move(lease));
    return is_lease_of_pool && !pool.is_exclusive_out() && pool.lend(test).is_some();
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    constexpr ::foundation::core::TextView kRefusal{"a SharedPermissionPool took the lease of another pool"};
    ::fixy::check::expect_dies(test, deposit_lease_of_another_pool, kRefusal);
    CRUCIBLE_TEST_ASSERT(test, deposit_own_lease());
    ::fixy::check::pass(test, "test_share_pool_double_deposit_aborts: refused\n");
    return 0;
}
