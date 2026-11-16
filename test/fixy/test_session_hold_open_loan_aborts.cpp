// A borrower that drops its hold without a release aborts with a named
// diagnostic.  Without the check, the lender waits for a release that
// never comes, which is the silent drop that LinearActris (Jacobs,
// Hinrichsen, Krebbers, POPL 2024) forbids.  The attack runs in a child
// process through expect_dies of the Check family, and the test passes
// only when the child ends by SIGABRT with that diagnostic.

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

void drop_open_loan() {
    namespace fp = ::foundation::permissions;
    namespace sess = ::fixy::session;
    auto lender = sess::mint_permission_hold(fp::mint_permission_root<Region>());
    auto [loan, lent] = std::move(lender).lend<Region>(1);
    auto [value, borrowing] = sess::mint_permission_hold().accept_loan(std::move(loan));
    (void)value;
    { auto dropped = std::move(borrowing); }
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(test, drop_open_loan,
                               ::fixy::TextView{"[Hold_Open_Loan]: a PermHold was destroyed with a loan open"});
    ::fixy::check::pass(test, "test_session_hold_open_loan_aborts: refused\n");
    return 0;
}
