// Three misuses of a DelegatedSession abort with a named diagnostic.
//
//   1. An accept on a DelegatedSession that a move emptied.  A second
//      accept would build a second handle over a Resource that moved.
//   2. A delegation with a hold that a move consumed.  The payload would
//      claim the token of a region that it does not hold.
//   3. An assignment over a DelegatedSession that holds a live endpoint.
//      The assignment drops that endpoint, and its policy acts.
//
// Each attack runs in a child process through expect_dies of the Check
// family.  The test passes only when the child ends by SIGABRT and its
// standard error names the rule.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Delegate.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Mint.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <utility>

namespace {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;

struct Wire {
    int sent = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Inner = s::Send<int, s::End>;

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using SendsRegion = s::Send<s::Transferable<int, Region>, s::End>;

void accept_after_a_move() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto parcel = s::mint_delegated_session(s::mint_session<Inner>(ctx, Wire{}));
    auto kept = std::move(parcel);
    auto second = std::move(parcel).accept();
    (void)kept;
    (void)second;
}

void delegate_with_a_consumed_hold() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto [handle, hold] = s::mint_permissioned_session<SendsRegion>(ctx, Wire{}, fp::mint_permission_root<Region>());
    auto kept = std::move(hold);
    auto parcel = s::mint_delegated_session(std::move(handle), std::move(hold));
    (void)kept;
    (void)parcel;
}

void assign_over_a_live_endpoint() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto target = s::mint_delegated_session(s::mint_session<Inner>(ctx, Wire{}));
    auto source = s::mint_delegated_session(s::mint_session<Inner>(ctx, Wire{}));
    target = std::move(source);
    (void)target;
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(test, accept_after_a_move, ::fixy::TextView{"ACCEPT OF AN EMPTY DELEGATION"});
    ::fixy::check::expect_dies(test, delegate_with_a_consumed_hold,
                               ::fixy::TextView{"[Hold_Consumed]: a PermHold was used after a move"});
    ::fixy::check::expect_dies(test, assign_over_a_live_endpoint,
                               ::fixy::TextView{"ABANDONMENT DETECTED (non-terminal handle)"});
    ::fixy::check::pass(test, "test_session_delegation_aborts: accept after a move, consumed hold and assignment over "
                              "a live endpoint refused\n");
    return 0;
}
