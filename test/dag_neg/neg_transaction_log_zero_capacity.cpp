// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating TransactionLog<N> with N = 0 (a zero-
// capacity ring).  A ring with no slots cannot hold a transaction;
// begin_tx() would claim a slot that does not exist.
//
// The ring storage of TransactionLog<N> is fixy::CyclicBuffer<Transaction,
// N>, and this fixture is the companion to the non-power-of-two one:
//   * The class-body static_assert((N & (N - 1)) == 0, ...) PASSES for
//     N = 0, because `0 & (0u - 1u)` is 0.
//   * The class-body static_assert(N >= 4, ...) refuses N = 0.  The
//     capacity fixtures pin that gate.
//   * CyclicBuffer<Transaction, 0>'s requires-clause
//     `(N > 0 && (N & (N - 1)) == 0)` also refuses N = 0, so the `Ring`
//     alias is ill-formed.  This fixture pins the requires-clause.
//
// Companion: neg_transaction_log_non_power_of_two.cpp (the non-power-of-two
// edge, caught by the power-of-two gate and the requires-clause).

#include <crucible/Transaction.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

// A valid owner, so the capacity is the one thing refused.
class Owner {
    Owner() noexcept {}

public:
    Owner(const Owner&) = delete;
    ~Owner() {}
};

int main() {
    // A valid context, so the log is refused for its capacity alone.
    crucible::TransactionLog<0, Owner> bad{::fixy::TestRunnerCtx{::foundation::effects::testing::test()}};
    (void)bad;
    return 0;
}
