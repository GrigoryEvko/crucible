// The claim of the exclusive of a pool at rest (claim_exclusive_at_rest of
// foundation/permissions/SharedPermission.h).
//
// An owner that ends with no context, such as the end of a channel that
// drains its ring, takes the exclusive of its pool with this door.  The owner
// holds the fact that no share and no lease is out, so the claim cannot fail
// in correct code.  A share out and a lease out each end the process.  Each
// attack runs in a child process through expect_dies of the Check family,
// and the test passes only when the child ends by SIGABRT with the refusal
// in its diagnostic.  A claim of an idle pool is the control.

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

// A share lives while the owner claims the pool at rest.
void claim_while_a_share_lives() {
    namespace fp = ::foundation::permissions;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    fp::SharedPermissionPool pool{fp::mint_permission_root<Region>()};
    auto share = pool.lend(test).expect("an idle pool lends a share");
    auto lease = pool.claim_exclusive_at_rest();
    pool.deposit_exclusive(std::move(lease));
    static_cast<void>(share.token());
}

// The lease is out while the owner claims the pool at rest.
void claim_while_the_lease_is_out() {
    namespace fp = ::foundation::permissions;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    fp::SharedPermissionPool pool{fp::mint_permission_root<Region>()};
    auto first = pool.try_upgrade(test).expect("an idle pool gives its exclusive");
    auto second = pool.claim_exclusive_at_rest();
    pool.deposit_exclusive(std::move(second));
    pool.deposit_exclusive(std::move(first));
}

// The control: an idle pool gives its exclusive at rest, takes it back, and
// then lends.
[[nodiscard]] bool claim_an_idle_pool() {
    namespace fp = ::foundation::permissions;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    fp::SharedPermissionPool pool{fp::mint_permission_root<Region>()};
    auto lease = pool.claim_exclusive_at_rest();
    bool const is_out = pool.is_exclusive_out() && lease.is_lease_of(pool);
    pool.deposit_exclusive(std::move(lease));
    return is_out && !pool.is_exclusive_out() && pool.lend(test).is_some();
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    constexpr ::foundation::core::TextView kRefusal{
        "a claim of the exclusive of a SharedPermissionPool at rest found a share or the lease out"};
    ::fixy::check::expect_dies(test, claim_while_a_share_lives, kRefusal);
    ::fixy::check::expect_dies(test, claim_while_the_lease_is_out, kRefusal);
    CRUCIBLE_TEST_ASSERT(test, claim_an_idle_pool());
    ::fixy::check::pass(test, "test_share_pool_claim_at_rest: refused\n");
    return 0;
}
