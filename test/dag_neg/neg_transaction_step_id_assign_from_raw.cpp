// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Transaction::step_id is fixy::Monotonic<uint64_t>.  A raw assignment
// `tx.step_id = n` must not compile, because it could write a smaller
// value and break the ordering of steps that replay rests on.  The only
// writes are advance(), which checks that the value does not go back, and
// try_advance(), which refuses a value that goes back.  Monotonic has no
// assignment from T, and its constructor from T is private.
//
// Companion: neg_transaction_step_id_advance_retrograde.cpp refuses an
// advance that goes back.

#include <crucible/Transaction.h>

#include <cstdint>

int main() {
    crucible::Transaction tx{};
    tx.step_id = uint64_t{42};
    (void)tx;
    return 0;
}
