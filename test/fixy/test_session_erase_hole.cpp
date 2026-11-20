// A session hold drops the brand of each token at its entry, so a token that
// leaves the hold is on the erased identity of its tag.  That token is no
// singleton root, and the live count of its tag is clear, so
// release_singleton_root refuses it and ends the process.  The release runs
// in a child process through expect_dies of the Check family.  A singleton
// root that goes through a hold comes back as the same root, and its release
// clears the count.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Flow.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <utility>

// A tag of a singleton root has a stable identity, so it has external
// linkage.
namespace test_session_erase_hole_tags {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace test_session_erase_hole_tags

namespace {

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

using test_session_erase_hole_tags::Region;

void release_a_token_of_a_hold() {
    auto hold = sess::mint_permission_hold(fp::mint_permission_root<Region>());
    auto [token] = std::move(hold).into_permissions();
    fp::release_singleton_root(std::move(token));
}

void singleton_root_through_a_hold() {
    auto hold = sess::mint_permission_hold(fp::mint_singleton_root<Region>());
    auto [root] = std::move(hold).into_permissions();
    fp::release_singleton_root(std::move(root));
    auto again = fp::mint_singleton_root<Region>();
    fp::release_singleton_root(std::move(again));
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    singleton_root_through_a_hold();
    ::fixy::check::expect_dies(test, release_a_token_of_a_hold,
                               ::fixy::TextView{"LinearityViolation: a token of the permission tag"});
    ::fixy::check::pass(test, "test_session_erase_hole: OK\n");
    return 0;
}
