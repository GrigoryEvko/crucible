// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Transaction::ts_ns holds an optional MonotonicClockBytes<std::uint64_t>,
// and only a clock reader builds such a reading.  A bare integer carries no
// source, so it does not become a reading: the constructor of ClockSource
// from its value is private.  If this fixture compiles, a transaction can
// claim a time that no clock returned.
//
// Companion: neg_transaction_ts_ns_cross_source.cpp refuses a reading of
// a different clock.

#include <crucible/Transaction.h>

#include <cstdint>

int main() {
    crucible::Transaction tx{};
    tx.ts_ns = std::uint64_t{42};
    return 0;
}
