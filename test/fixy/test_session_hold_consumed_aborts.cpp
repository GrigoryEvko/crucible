// A transition on a hold that a move consumed aborts with a named
// diagnostic.  A hold that could run twice would spend its tokens twice:
// the second take below would hand out the same token a second time.
// The attack runs in a child process through expect_dies of the Check
// family, and the test passes only when the child ends by SIGABRT with that
// diagnostic.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Flow.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

void take_twice() {
    namespace fp = ::foundation::permissions;
    namespace sess = ::fixy::session;
    auto hold = sess::mint_permission_hold(fp::mint_permission_root<Region>());
    auto moved = std::move(hold);
    auto first = std::move(moved).take<Region>();
    auto second = std::move(hold).take<Region>();
    (void)first;
    (void)second;
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(test, take_twice, ::fixy::TextView{"[Hold_Consumed]: a PermHold was used after a move"});
    ::fixy::check::pass(test, "test_session_hold_consumed_aborts: refused\n");
    return 0;
}
