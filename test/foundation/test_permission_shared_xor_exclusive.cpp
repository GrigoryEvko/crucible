// The pool exists to keep shared access and exclusive access mutually
// exclusive. The attack on that invariant is to copy a shared token out of
// a guard, stash it past the guard's scope, and then upgrade: an exclusive
// permission is handed out while a shared token is still live.
//
// The resolution is that a shared token confers no runtime access at all.
// It is a type-level witness. The invariant is enforced over guards, which
// are the access carriers: a guard is live exactly while the count is
// above zero, and the upgrade fails exactly then. These tests pin that.

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/SharedPermission.h>

#include <optional>
#include <utility>

using namespace foundation::permissions;

namespace {

using ::fixy::check::pass;

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

static_assert(SharedPermission<Region>::confers_runtime_access == false,
              "SharedPermission must confer no runtime access");

// The attack run verbatim. Because the access carrier is the guard and not
// the token, the upgrade is blocked exactly while a guard is live, however
// many tokens were copied out and stashed.
void test_stashed_token_does_not_unblock_upgrade(::fixy::TestRunnerCtx const& test) {
    auto exc = mint_permission_root<Region>();
    SharedPermissionPool pool{std::move(exc)};

    CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 0);
    CRUCIBLE_TEST_ASSERT(test, !pool.is_exclusive_out());

    std::optional<SharedPermission<Region, decltype(pool)::brand_type>> stashed_token;
    {
        auto guard = pool.lend().expect("an idle pool lends a share");
        CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 1);

        // The attack step: copy the token out, past the guard's scope.
        stashed_token = guard.token();

        auto blocked = pool.try_upgrade();
        CRUCIBLE_TEST_ASSERT(test, !blocked.has_value());
        CRUCIBLE_TEST_ASSERT(test, !pool.is_exclusive_out());
    }
    // The guard is gone and the count is back to zero. The stashed token is
    // still alive, but it carries no access, so no shared reader survives
    // and the upgrade below is sound.
    CRUCIBLE_TEST_ASSERT(test, stashed_token.has_value());
    CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 0);

    auto upgraded = pool.try_upgrade();
    CRUCIBLE_TEST_ASSERT(test, upgraded.has_value());
    CRUCIBLE_TEST_ASSERT(test, pool.is_exclusive_out());

    // The other half of the invariant: while the exclusive permission is
    // out, lending must fail until it is deposited back.
    auto cannot_lend = pool.lend();
    CRUCIBLE_TEST_ASSERT(test, cannot_lend.is_none());

    pool.deposit_exclusive(std::move(*upgraded));
    CRUCIBLE_TEST_ASSERT(test, !pool.is_exclusive_out());

    auto relent = pool.lend();
    CRUCIBLE_TEST_ASSERT(test, relent.is_some());
    CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 1);
}

void test_multiple_guards_block_upgrade(::fixy::TestRunnerCtx const& test) {
    auto exc = mint_permission_root<Region>();
    SharedPermissionPool pool{std::move(exc)};

    auto g1 = pool.lend();
    auto g2 = pool.lend();
    CRUCIBLE_TEST_ASSERT(test, g1.is_some() && g2.is_some());
    CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 2);

    // A token from each guard, to show that copying one out does not move
    // the carrier count.
    for (auto const& guard : g1) {
        [[maybe_unused]] auto t1 = guard.token();
    }
    for (auto const& guard : g2) {
        [[maybe_unused]] auto t2 = guard.token();
    }

    // Each refused upgrade gives an empty std::optional.  The answer has a
    // name, so the temporary ends in this file and not in the macro.
    bool const is_refused_under_two_guards = !pool.try_upgrade().has_value();
    CRUCIBLE_TEST_ASSERT(test, is_refused_under_two_guards);

    g1 = ::foundation::core::none;
    CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 1);
    bool const is_refused_under_one_guard = !pool.try_upgrade().has_value();
    CRUCIBLE_TEST_ASSERT(test, is_refused_under_one_guard);

    g2 = ::foundation::core::none;
    CRUCIBLE_TEST_ASSERT(test, pool.outstanding() == 0);

    auto up = pool.try_upgrade();
    CRUCIBLE_TEST_ASSERT(test, up.has_value());
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    pass(test, "test_permission_shared_xor_exclusive:\n");
    test_stashed_token_does_not_unblock_upgrade(test);
    pass(test, "  stashed_token_does_not_unblock_upgrade: PASSED\n");
    test_multiple_guards_block_upgrade(test);
    pass(test, "  multiple_guards_block_upgrade: PASSED\n");
    return 0;
}
