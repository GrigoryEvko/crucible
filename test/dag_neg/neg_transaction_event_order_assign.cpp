// Negative compile test.  This file must fail to compile.
//
// Transaction::event_order is the place of the last event of a transaction
// in the order of the events of its log.  The log advances it, and the place
// only grows.  A Monotonic refuses an assignment, so no code writes a place
// backward.
//
// Expected diagnostic: the copy assignment of the Monotonic is deleted.

#include <crucible/Transaction.h>

#include <cstdint>

int main() {
    crucible::Transaction tx{};
    tx.event_order = ::fixy::mint_monotonic<std::uint64_t>(0);
    return 0;
}
