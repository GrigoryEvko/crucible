// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The live-slot pointer of TransactionLog is
// fixy::Tagged<Transaction*, source::Ring>.  A pointer under the Arena tag
// wraps the same pointer type, but the tag makes the two distinct types,
// and no retag edge joins Arena to Ring.  An arena pointer is freed at
// arena reset, and a ring pointer lives as long as the log.
//
// Companion: neg_active_tx_ptr_raw_assignment.cpp refuses an untagged
// pointer.

#include <fixy/Tagged.h>
#include <fixy/Tags.h>

namespace crucible {
struct FakeTransaction {
    int dummy;
};
}  // namespace crucible

int main() {
    using RingTx = ::fixy::Tagged<crucible::FakeTransaction*, ::fixy::tags::source::Ring>;

    crucible::FakeTransaction tx{};
    auto arena_tagged = ::fixy::mint_tagged<::fixy::tags::source::Arena>(&tx);

    RingTx field = arena_tagged;
    (void)field;
    return 0;
}
