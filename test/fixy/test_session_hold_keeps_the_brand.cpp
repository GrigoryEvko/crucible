// A session hold keeps the brand of each token that it takes, so a token that
// leaves the hold is the token that entered it.  The token of a root of
// mint_permission_root leaves with the brand of its root.  A singleton root
// leaves on the erased identity as the same root, so its release clears the
// live count of its tag, and the next singleton mint of the tag takes the tag
// again.  The fixtures neg_sess_hold_token_is_no_singleton_root and
// neg_sess_received_token_is_no_singleton_root refuse the release of a token
// of a brand that comes out of a hold or out of a session.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Flow.h>
#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

// A tag of a singleton root has a stable identity, so it has external
// linkage.
namespace test_session_hold_keeps_the_brand_tags {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Other {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace test_session_hold_keeps_the_brand_tags

namespace {

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

using test_session_hold_keeps_the_brand_tags::Other;
using test_session_hold_keeps_the_brand_tags::Region;

// The tokens of two roots leave the hold with the brands of their roots, in
// the order of the set, also after a take and a put.
void roots_through_a_hold() {
    auto region_root = fp::mint_permission_root<Region>();
    auto other_root = fp::mint_permission_root<Other>();
    using RegionToken = decltype(region_root);
    using OtherToken = decltype(other_root);
    static_assert(::foundation::brand::IsBranded<RegionToken> && ::foundation::brand::IsBranded<OtherToken>);
    auto hold = sess::mint_permission_hold(std::move(region_root), std::move(other_root));
    static_assert(std::is_same_v<typename decltype(hold)::perm_set, fp::PermSet<RegionToken, OtherToken>>,
                  "the set of a hold names each token with its brand");
    auto [taken, rest] = std::move(hold).template take<Region>();
    static_assert(std::is_same_v<decltype(taken), RegionToken>, "a take gives the token with its brand");
    auto again = std::move(rest).put(std::move(taken));
    auto [region_token, other_token] = std::move(again).into_permissions();
    static_assert(std::is_same_v<decltype(region_token), RegionToken>
                      && std::is_same_v<decltype(other_token), OtherToken>,
                  "the end of a hold gives back the tokens that it took");
    ::foundation::linear::drop(std::move(region_token));
    ::foundation::linear::drop(std::move(other_token));
}

void singleton_root_through_a_hold() {
    auto hold = sess::mint_permission_hold(fp::mint_singleton_root<Region>());
    auto [root] = std::move(hold).into_permissions();
    static_assert(::foundation::brand::IsErased<decltype(root)>, "a singleton root stays on the erased identity");
    fp::release_singleton_root(std::move(root));
    auto again = fp::mint_singleton_root<Region>();
    fp::release_singleton_root(std::move(again));
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    roots_through_a_hold();
    singleton_root_through_a_hold();
    ::fixy::check::pass(test, "test_session_hold_keeps_the_brand: OK\n");
    return 0;
}
