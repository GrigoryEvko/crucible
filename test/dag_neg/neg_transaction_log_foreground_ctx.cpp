// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A transaction log stamps each transaction with the monotonic clock, so
// it holds a clock reader.  A clock read on the replay-bound foreground
// path makes replay diverge across machines, and the foreground context
// owns none of Bg, Init or Test.  CtxFitsTransactionLog refuses it.
//
// Companion: neg_transaction_log_bg_cap_without_bg_row.cpp refuses a
// context that holds a background capability but does not claim the
// background effect.

#include <crucible/Transaction.h>
#include <foundation/effects/Ctx.h>

class Owner {
    Owner() noexcept {}

public:
    Owner(const Owner&) = delete;
    ~Owner() {}
};

int main() {
    crucible::TransactionLog<16, Owner> log{::foundation::effects::testing::foreground()};
    (void)log;
    return 0;
}
