// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The live slot of TransactionLog is
// fixy::Tagged<Option<Ref<Transaction>>, source::Ring>.  An Option of a Ref
// with no tag does not convert to it, because the constructor from the value
// is private and mint_tagged<source::Ring> is the one door, which names at
// the call site that the transaction came from the inline slots of the ring.
//
// Companion: neg_active_tx_ptr_cross_source_assignment.cpp refuses a slot
// under a different source tag.

#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

namespace crucible {
struct FakeTransaction {
    int dummy;
};
}  // namespace crucible

int main() {
    using TxLink = ::fixy::Option<::fixy::Ref<crucible::FakeTransaction>>;
    using ActiveTxLink = ::fixy::Tagged<TxLink, ::fixy::tags::source::Ring>;

    crucible::FakeTransaction tx{};
    const TxLink untagged = ::fixy::some(::fixy::erase_brand(::fixy::to(tx)));

    ActiveTxLink field = untagged;
    (void)field;
    return 0;
}
