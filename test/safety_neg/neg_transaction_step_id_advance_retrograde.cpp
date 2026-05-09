// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for #1060 WRAP-Transaction-1
// (Transaction::step_id uint64_t → safety::Monotonic<uint64_t>).
//
// Premise: with step_id migrated to Monotonic<uint64_t>, calling
// advance() with a value strictly less than the current value violates
// Monotonic's pre-clause `lattice_type::leq(impl_.peek(), new_value)`.
// In a constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Distinct mismatch class from companion fixture
// neg_transaction_step_id_assign_from_raw.cpp:
//   * Companion:    WRITE-side raw-assign gate (no operator=(T)).
//   * This fixture: ADVANCE-side retrograde-write gate (the
//     pre-clause fires at consteval when new < current, catching the
//     entire class of `step_id.advance(smaller)` bugs that would
//     break the global monotonicity guarantee TransactionLog::count_
//     enforces at the LOG level).
//
// Two fixtures cover both directions of the new soundness gate per HS14.

#include <crucible/safety/Mutation.h>

#include <cstdint>

constexpr uint64_t exercise_retrograde() {
    crucible::safety::Monotonic<uint64_t> step{100};
    // Retrograde write: 100 → 50 violates the pre-clause
    // `lattice_type::leq(100, 50)` ≡ `!(50 < 100)` → false.  Under
    // constexpr evaluation, contract violation poisons the expression.
    step.advance(uint64_t{50});  // ← MUST fail at consteval
    return step.get();
}

int main() {
    constexpr uint64_t result = exercise_retrograde();
    (void)result;
    return 0;
}
