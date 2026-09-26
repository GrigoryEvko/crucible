// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating TransactionLog<N> with a non-power-of-2 N
// (here N = 7).  The ring's MRU index arithmetic — `& (N - 1)` masking
// in the write cursor — is only a correct modulo when N is a power of
// two; for N = 7 the mask 6 = 0b110 skips slot indices and the wrap
// invariant breaks.
//
// The ring storage of TransactionLog<N> is fixy::CyclicBuffer<Transaction,
// N>.  Two independent gates reject a non-power-of-2 N:
//   (1) the class-body static_assert((N & (N - 1)) == 0, ...), the
//       TransactionLog-local diagnostic ("N must be a power of 2"); and
//   (2) CyclicBuffer<Transaction, N>'s own requires-clause
//       `(N > 0 && (N & (N - 1)) == 0)`, which makes the `Ring` alias
//       ill-formed for N = 7.
//
// Why it must reject: a masked cursor over a non-power-of-2 ring would
// alias distinct logical positions onto the same physical slot, so
// previous()'s backward walk and begin_tx's slot recycling would both
// corrupt.
//
// Companion: neg_transaction_log_zero_capacity.cpp, which slips past
// gate (1) and is caught only by gate (2).

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
    crucible::TransactionLog<7, Owner> bad{::fixy::TestRunnerCtx{::foundation::effects::testing::test()}};
    (void)bad;
    return 0;
}
