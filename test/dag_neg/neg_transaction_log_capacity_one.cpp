// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating TransactionLog<N> with N = 1.  A claim in a
// full ring keeps the slot of the active transaction and the slot of the
// rollback target, and takes a third slot.  A ring of one slot has no third
// slot, and begin_tx() would recycle a slot that must stay.
//
// One is a power of two, so the power-of-two static_assert passes, and so
// does the requires-clause of CyclicBuffer<Transaction, 1>.  Only the
// static_assert(N >= 4, ...) of the class body refuses the log.
//
// Companions: neg_transaction_log_capacity_two.cpp,
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
    crucible::TransactionLog<1, Owner> bad{::fixy::TestRunnerCtx{::foundation::effects::testing::test()}};
    (void)bad;
    return 0;
}
