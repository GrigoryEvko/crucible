// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Transaction-3 #1062, mismatch class #1 of 2:
// BARE-u64 ASSIGNMENT IS REJECTED.
//
// After the migration, Transaction::ts_ns is
// MonotonicClockBytes<std::uint64_t> (regime-1 EBO-collapsed) instead
// of bare uint64_t.  This pins the clock-source provenance at the
// type level: a bare scalar carries no source witness, so assigning
// raw std::uint64_t{42} to ts_ns must be statically rejected.  If
// this fixture starts COMPILING, the wrap silently regressed to
// bare uint64_t and every clock-source guarantee on Transaction
// disappeared.
//
// Distinct from the cross-source fixture, which fails because two
// DIFFERENT wrapped types collide; here the failure is a bare scalar
// trying to land in a wrapped slot.
//
// Expected diagnostic: no match for 'operator=' / cannot convert /
// no viable / conversion from.

#include <crucible/Transaction.h>

#include <cstdint>

int main() {
    crucible::Transaction tx{};

    // Should FAIL: bare std::uint64_t has no implicit conversion to
    // safety::MonotonicClockBytes<std::uint64_t>.
    tx.ts_ns = std::uint64_t{42};

    return 0;
}
