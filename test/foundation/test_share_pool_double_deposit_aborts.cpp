// A pool that still parks its exclusive refuses a second deposit, and a
// pool that lends a share refuses a deposit.
//
// A brand names a mint site, so two root mints through one site are two
// tokens of one type, and either fits the pool.  Without the check of the
// state in deposit_exclusive, the second deposit overwrites the parked
// token, and the one region has had two owners.  A deposit into a pool
// that lends stores zero over the live count, and the next upgrade takes
// the region from under a reader.  Each attack runs in a child process
// through expect_dies of the Check family, and the test passes only when
// the child ends by SIGABRT with the failed condition in its diagnostic.
//
// Two targets build this file.  test_share_pool_deposit_under_ignore
// builds it with the ignore semantic, where a precondition is only an
// assumption.  So the check of the state must be a fatal invariant.

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

void deposit_while_parked() {
    namespace fp = ::foundation::permissions;
    auto first = mint_region();
    auto second = mint_region();
    fp::SharedPermissionPool pool{std::move(first)};
    pool.deposit_exclusive(std::move(second));
}

// The body runs under one lent share of the pool, and deposits there.
void deposit_while_lending() {
    namespace fp = ::foundation::permissions;
    auto first = mint_region();
    auto second = mint_region();
    fp::SharedPermissionPool pool{std::move(first)};
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    fp::with_shared_read(test, pool, [&pool, token = std::move(second)](auto) mutable noexcept {
        pool.deposit_exclusive(std::move(token));
    });
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    constexpr ::foundation::core::TextView kRefusal{"fatal invariant failed: is_exclusive_out()"};
    ::fixy::check::expect_dies(test, deposit_while_parked, kRefusal);
    ::fixy::check::expect_dies(test, deposit_while_lending, kRefusal);
    ::fixy::check::pass(test, "test_share_pool_double_deposit_aborts: refused\n");
    return 0;
}
