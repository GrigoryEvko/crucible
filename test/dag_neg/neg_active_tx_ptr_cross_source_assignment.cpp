// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The live slot of TransactionLog is
// fixy::Tagged<Option<Ref<Transaction>>, source::Ring>.  A slot under the
// Arena tag wraps the same payload type, but the tag makes the two distinct
// types, and no retag edge joins Arena to Ring.  An arena object is freed at
// arena reset, and a slot of the ring lives as long as the log.
//
// Companion: neg_active_tx_ptr_raw_assignment.cpp refuses an untagged
// slot.

#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

namespace crucible {
struct FakeTransaction {
    int dummy;
};
}  // namespace crucible

int main() {
    using RingTx = ::fixy::Tagged<::fixy::Option<::fixy::Ref<crucible::FakeTransaction>>, ::fixy::tags::source::Ring>;

    crucible::FakeTransaction tx{};
    auto arena_tagged =
        ::fixy::mint_tagged<::fixy::tags::source::Arena>(::fixy::some(::fixy::erase_brand(::fixy::to(tx))));

    RingTx field = arena_tagged;
    (void)field;
    return 0;
}
