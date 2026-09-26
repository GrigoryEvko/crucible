// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The live-slot pointer of TransactionLog is
// fixy::Tagged<Transaction*, source::Ring>.  A raw Transaction* does not
// convert to it, because the constructor from the value is private and
// mint_tagged<source::Ring> is the one door, which names at the call site
// that the pointer came from the ring's inline slots.
//
// Companion: neg_active_tx_ptr_cross_source_assignment.cpp refuses a
// pointer under a different source tag.

#include <fixy/Tagged.h>
#include <fixy/Tags.h>

namespace crucible {
struct FakeTransaction {
    int dummy;
};
}  // namespace crucible

int main() {
    using ActiveTxPtr = ::fixy::Tagged<crucible::FakeTransaction*, ::fixy::tags::source::Ring>;

    crucible::FakeTransaction tx{};
    crucible::FakeTransaction* raw_ptr = &tx;

    ActiveTxPtr field = raw_ptr;
    (void)field;
    return 0;
}
