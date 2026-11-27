// Negative-compile test.  This file must fail to compile.
//
// TraceRing::reset moves the counters of a ring back to the start.  Two
// holders of one class hold two held splits of one type, and the door of two
// held sides takes a side of each, because the producer side and the consumer
// side have two types.  The two ends would give a reset of two rings, whose
// consumer a second thread can hold.  The reset takes the pair of the two
// ends of one split, and only a pair door builds a pair, so two ends of two
// rings reach no reset.
//
// Expected diagnostic: the reset takes a pair, not two ends.

#include <crucible/TraceRing.h>
#include <fixy/concurrent/SpscEnds.h>
#include <foundation/effects/Effect.h>

namespace {

class ProducerProof final {
    ProducerProof() noexcept = default;
    friend struct ProofDoor;

public:
    ProducerProof(ProducerProof const&) = delete("a proof lives for its call");
    ~ProducerProof() = default;
};
class ConsumerProof final {
    ConsumerProof() noexcept = default;
    friend struct ProofDoor;

public:
    ConsumerProof(ConsumerProof const&) = delete("a proof lives for its call");
    ~ConsumerProof() = default;
};
struct ProofDoor final {
    static ProducerProof producer() noexcept { return ProducerProof{}; }
    static ConsumerProof consumer() noexcept { return ConsumerProof{}; }
};
struct HeldBrand final {
    using producer_proof = ProducerProof;
    using consumer_proof = ConsumerProof;
};

}  // namespace

int main() {
    auto first = ::fixy::concurrent::mint_held_spsc_split<crucible::TraceRing, HeldBrand>(
        ::foundation::effects::testing::test().alloc);
    auto second = ::fixy::concurrent::mint_held_spsc_split<crucible::TraceRing, HeldBrand>(
        ::foundation::effects::testing::test().alloc);
    ProducerProof const producer = ProofDoor::producer();
    ConsumerProof const consumer = ProofDoor::consumer();
    ::fixy::concurrent::with_held_ends(
        first.producer_side(), producer, second.consumer_side(), consumer,
        [](crucible::TraceRing::Producer& ring_producer, crucible::TraceRing::Consumer& ring_consumer) noexcept {
            crucible::TraceRing::reset(ring_producer, ring_consumer);
        });
    return 0;
}
