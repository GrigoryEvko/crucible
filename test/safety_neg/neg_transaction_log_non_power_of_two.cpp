// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating TransactionLog<N> with a non-power-of-2 N
// (here N = 7).  The ring's MRU index arithmetic — `& (N - 1)` masking
// in the write cursor — is only a correct modulo when N is a power of
// two; for N = 7 the mask 6 = 0b110 skips slot indices and the wrap
// invariant breaks.
//
// Per WRAP-Transaction-4 (#1063), TransactionLog<N>'s ring storage is
// now safety::CyclicBuffer<Transaction, N> (entries_ + head_ + count_
// collapsed into one audited composition).  TWO independent gates
// reject a non-power-of-2 N:
//   (1) the class-body static_assert((N & (N - 1)) == 0, ...) — the
//       clear, TransactionLog-local diagnostic ("N must be a power of
//       2"); and
//   (2) CyclicBuffer<Transaction, N>'s own requires-clause
//       `(N > 0 && (N & (N - 1)) == 0)`, which makes the `Ring` alias
//       ill-formed for N = 7.
// This is the NON-POWER-OF-2 rejection — distinct from the companion
// fixture's ZERO-CAPACITY rejection (= N = 0), which slips past gate
// (1) and is caught only by gate (2).
//
// Why it must reject: a masked cursor over a non-power-of-2 ring would
// alias distinct logical positions onto the same physical slot, so
// previous()'s backward walk and begin_tx's slot recycling would both
// corrupt — exactly the "circular index after first wrap" bug class
// CyclicBuffer was promoted to eliminate by construction.
//
// Per HS14, ≥2 negative-compile fixtures per soundness gate, each a
// distinct mismatch class.  Companion:
// neg_transaction_log_zero_capacity.cpp.

#include <crucible/Transaction.h>

int main() {
    // Bridge fires: 7 & 6 == 6 != 0 → the power-of-2 static_assert fails
    // AND CyclicBuffer<Transaction, 7>'s requires-clause is unsatisfied.
    crucible::TransactionLog<7> bad;
    (void)bad;
    return 0;
}
