// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Transaction::step_id is fixy::Monotonic<uint64_t>.  An advance to a
// value below the current one breaks the precondition of advance(), which
// says that the new value does not go back.  In a constant evaluation the
// broken precondition makes the expression not constant, so the program is
// ill-formed where a constant is required.
//
// Companion: neg_transaction_step_id_assign_from_raw.cpp refuses a raw
// assignment.

#include <crucible/Transaction.h>

#include <cstdint>

constexpr uint64_t exercise_retrograde() {
    crucible::Transaction tx{};
    tx.step_id.advance(uint64_t{100});
    tx.step_id.advance(uint64_t{50});
    return tx.step_id.get();
}

int main() {
    constexpr uint64_t result = exercise_retrograde();
    (void)result;
    return 0;
}
