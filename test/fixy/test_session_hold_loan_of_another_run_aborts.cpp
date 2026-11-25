// A hold that ends its loan with the release of another hold aborts with a
// named diagnostic.  Two holds of one root mint site have one type, and so do
// their releases.  Without the check, the token of the first hold comes back
// while the first borrower still reads, so a writer and a reader of one region
// live at one time.  The attack runs in a child process through expect_dies of
// the Check family, and the test passes only when the child ends by SIGABRT
// with that diagnostic.  The test also closes the two loans with their own
// releases, in the process of the test.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Flow.h>
#include <foundation/Linear.h>
#include <foundation/effects/Effect.h>

#include <type_traits>
#include <utility>

namespace {

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

// Ends the loan of a lender with the release of a borrower, and gives the
// value of the release.  The two can belong to two loans.
template <class Lender, class Borrower>
[[nodiscard]] int end_loan(Lender lender, Borrower borrower) noexcept {
    auto [release, borrower_done] = std::move(borrower).template release<Region>(2);
    auto [value, lender_closed] = std::move(lender).end_loan(std::move(release));
    auto [token, rest] = std::move(lender_closed).template take<Region>();
    ::foundation::linear::drop(std::move(token));
    static_cast<void>(std::move(rest).into_permissions());
    static_cast<void>(std::move(borrower_done).into_permissions());
    return value;
}

// Which borrower sends its release to which lender, of two loans.
enum class ReleaseOrder : bool {
    own,
    crossed
};

// Lends a region of one root mint site to a borrower.  The template calls
// itself one time, so its two instantiations run one call of the mint, and
// the two loans have one brand and one type.  The second call gives each
// release to the lender that the order names.
template <ReleaseOrder order, class... First>
[[nodiscard]] int close_two_loans(First&&... first) noexcept {
    auto [loan, lent] = sess::mint_permission_hold(fp::mint_permission_root<Region>()).template lend<Region>(1);
    auto [ticket, borrowing] = sess::mint_permission_hold().accept_loan(std::move(loan));
    if constexpr (sizeof...(First) == 0) {
        return ticket + close_two_loans<order>(std::move(lent), std::move(borrowing));
    } else {
        return [ticket](auto first_lender, auto first_borrower, auto second_lender, auto second_borrower) noexcept {
            static_assert(std::is_same_v<decltype(first_lender), decltype(second_lender)>);
            static_assert(std::is_same_v<decltype(first_borrower), decltype(second_borrower)>);
            // The first borrower reads its region while the second loan is open.
            auto [seen, first_reading] =
                std::move(first_borrower).template read<Region>([](auto const&) noexcept { return 1; });
            if constexpr (order == ReleaseOrder::own) {
                const int first_value = end_loan(std::move(first_lender), std::move(first_reading));
                return ticket + seen + first_value + end_loan(std::move(second_lender), std::move(second_borrower));
            } else {
                const int first_value = end_loan(std::move(first_lender), std::move(second_borrower));
                return ticket + seen + first_value + end_loan(std::move(second_lender), std::move(first_reading));
            }
        }(std::forward<First>(first)..., std::move(lent), std::move(borrowing));
    }
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    CRUCIBLE_TEST_ASSERT(test, close_two_loans<ReleaseOrder::own>() == 7);
    ::fixy::check::expect_dies(
        test, [] noexcept { static_cast<void>(close_two_loans<ReleaseOrder::crossed>()); },
        ::fixy::TextView{"fatal invariant failed: lent.identity_.pairs_with(loan.identity_)"});
    ::fixy::check::pass(test, "test_session_hold_loan_of_another_run_aborts: refused\n");
    return 0;
}
