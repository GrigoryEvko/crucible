// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating TransactionLog<N> with N = 0 (a zero-
// capacity ring).  A ring with no slots cannot hold a transaction;
// begin_tx() would claim a slot that does not exist.
//
// Per WRAP-Transaction-4 (#1063), TransactionLog<N>'s ring storage is
// safety::CyclicBuffer<Transaction, N>.  N = 0 is the mismatch class
// the migration STRENGTHENED the contract against, and it is precisely
// why this fixture is the companion to the non-power-of-2 one:
//   * The class-body static_assert((N & (N - 1)) == 0, ...) PASSES for
//     N = 0 — `0 & (0u - 1u)` is `0 & 0xFFFFFFFF == 0`, vacuously a
//     "power of two".  So the local gate does NOT catch a zero ring.
//   * CyclicBuffer<Transaction, 0>'s requires-clause
//     `(N > 0 && (N & (N - 1)) == 0)` DOES catch it — `0 > 0` is
//     false → the `Ring` alias is ill-formed → TransactionLog<0> has
//     no valid specialization.
// Before #1063 the ring was a hand-rolled `Transaction entries_[0]`
// (an ill-formed-but-tolerated zero-size array) with no N > 0 gate;
// the CyclicBuffer migration adds the positive-capacity guarantee at
// the type level.  This fixture is the witness that it fires.
//
// Per HS14, ≥2 negative-compile fixtures per soundness gate, each a
// distinct mismatch class.  Companion:
// neg_transaction_log_non_power_of_two.cpp (the non-power-of-2 edge,
// caught by BOTH gates).

#include <crucible/Transaction.h>

int main() {
    // Bridge fires: the power-of-2 static_assert passes for N = 0, but
    // CyclicBuffer<Transaction, 0>'s `requires (N > 0 ...)` rejects the
    // Ring alias → no valid TransactionLog<0> specialization.
    crucible::TransactionLog<0> bad;
    (void)bad;
    return 0;
}
