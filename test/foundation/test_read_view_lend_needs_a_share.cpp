// A share guard that no longer holds its share cannot lend a read view.
//
// A guard moved into another object keeps no pool.  If with_read_view
// took it, the body would read the region under no share, while the
// guard that holds the share can end it.  The door checks that the guard
// holds a share and aborts when it does not.  The attack moves the guard
// into a second guard and then lends the moved-from guard.  It reaches
// the moved-from guard through a reference, which the rule use-after-move
// does not read, the shape of a real caller that holds the guard in a
// structure.  It runs in a child process through expect_dies of the Check
// family, and the test passes only when the child aborts on that check.

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>
#include <foundation/permissions/SharedPermission.h>

#include <utility>

namespace test_read_view_lend_needs_a_share {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

// The guard that a call gives back by reference.
template <class Guard>
[[nodiscard]] static Guard& through(Guard& guard) noexcept {
    return guard;
}

static void lend_a_guard_that_gave_its_share_away() {
    namespace fp = ::foundation::permissions;
    auto root = fp::mint_permission_root<Region>();
    using Brand = decltype(root)::brand_type;
    fp::SharedPermissionPool<Region, Brand> pool{std::move(root)};
    fp::SharedPermissionGuard<Region, Brand> lent = pool.lend().expect("an idle pool lends a share");
    fp::SharedPermissionGuard<Region, Brand> holder{std::move(through(lent))};
    auto back = fp::with_read_view(std::move(through(lent)), [](auto const&) noexcept {});
    (void)back;
    (void)holder;
}

// The positive control: the guard that holds the share lends the view,
// and the door hands the guard back.
[[nodiscard]] static bool a_guard_that_holds_its_share_lends() {
    namespace fp = ::foundation::permissions;
    auto root = fp::mint_permission_root<Region>();
    using Brand = decltype(root)::brand_type;
    fp::SharedPermissionPool<Region, Brand> pool{std::move(root)};
    fp::SharedPermissionGuard<Region, Brand> lent = pool.lend().expect("an idle pool lends a share");
    bool read = false;
    auto back = fp::with_read_view(std::move(lent), [&read](auto const&) noexcept { read = true; });
    return read && back.holds_share();
}

}  // namespace test_read_view_lend_needs_a_share

int main() {
    using namespace test_read_view_lend_needs_a_share;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    CRUCIBLE_TEST_ASSERT(test, a_guard_that_holds_its_share_lends());
    ::fixy::check::expect_dies(test, lend_a_guard_that_gave_its_share_away,
                               ::foundation::core::TextView{"foundation: contract violation: source.holds_share()"});
    ::fixy::check::pass(test, "test_read_view_lend_needs_a_share: control lends, attack refused\n");
    return 0;
}
