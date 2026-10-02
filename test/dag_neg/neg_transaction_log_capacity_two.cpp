// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating TransactionLog<N> with N = 2, the largest power
// of two below the minimum ring.  A claim in a full ring keeps the slot of
// the active transaction and the slot of the rollback target.  In a ring of
// two slots, these two slots can be all of the ring.  Then begin_tx() has no
// slot to claim.
//
// Two is a power of two, so the power-of-two static_assert passes, and so
// does the requires-clause of CyclicBuffer<Transaction, 2>.  Only the
// static_assert(N >= 4, ...) of the class body refuses the log.
//
// Companions: neg_transaction_log_capacity_one.cpp,
// neg_transaction_log_zero_capacity.cpp and
// neg_transaction_log_non_power_of_two.cpp.

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
    crucible::TransactionLog<2, Owner> bad{::fixy::TestRunnerCtx{::foundation::effects::testing::test()}};
    (void)bad;
    return 0;
}
