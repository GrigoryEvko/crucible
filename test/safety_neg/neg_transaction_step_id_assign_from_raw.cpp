// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for #1060 WRAP-Transaction-1
// (Transaction::step_id uint64_t → safety::Monotonic<uint64_t>).
//
// Premise: with step_id migrated to Monotonic<uint64_t>, raw assignment
// `tx->step_id = some_uint64` must be rejected by the type system.  The
// only legitimate write paths are advance() (contract-checked
// non-retrograde) and try_advance() (returns false on retrograde).
// Pre-migration the field was bare uint64_t and any caller could write
// any value, including a smaller one (which would corrupt
// log_at_step_id() binary search and break replay-determinism per
// CLAUDE.md §II DetSafe).
//
// Distinct mismatch class from companion fixture
// neg_transaction_step_id_implicit_to_raw.cpp:
//   * This fixture: WRITE-side gate (raw assignment rejected because
//     Monotonic has no operator=(T) — only the inherited
//     defaulted-copy/move from Monotonic<T>).
//   * Companion:    READ-side gate (Monotonic<uint64_t> has no
//     implicit conversion to uint64_t — accidental
//     `uint64_t s = tx->step_id` rejected; legitimate reads go
//     through .get()).
//
// Two fixtures cover both directions of the new soundness gate per HS14.

#include <crucible/ir001/Transaction.h>

#include <cstdint>

int main() {
    crucible::Transaction tx{};
    // Monotonic<uint64_t> has no operator=(uint64_t) — the only
    // assignments are the defaulted copy/move from another Monotonic.
    // A future refactor that re-introduces a converting assignment
    // would silently re-admit retrograde writes.
    tx.step_id = uint64_t{42};   // ← MUST fail: no viable operator=
    (void)tx;
    return 0;
}
