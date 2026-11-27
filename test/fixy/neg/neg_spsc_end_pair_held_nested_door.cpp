// A body of the pair door of a held split borrows the split and the proof,
// and opens a second frame of the producer side.  The two bodies would hold
// the one producer end two times.  The pair door refuses a body that reaches
// a side of its proofs through a borrow, also through the held split.

#include <fixy/concurrent/SpscEnds.h>

#include <foundation/effects/Effect.h>

namespace {

struct Owner final {
    struct producer_state {};
    struct consumer_state {};
    Owner() noexcept = default;
    Owner(Owner const&) = delete("pinned");
    Owner(Owner&&) = delete("pinned");
};

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
    auto split =
        ::fixy::concurrent::mint_held_spsc_split<Owner, HeldBrand>(::foundation::effects::testing::test().alloc);
    ProducerProof const producer = ProofDoor::producer();
    ConsumerProof const consumer = ProofDoor::consumer();
    split.with_end_pair(producer, consumer, [&split, &producer](::fixy::concurrent::SpscEndPair<Owner>&) noexcept {
        split.producer_side().with_end(producer, [](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {});
    });
    return 0;
}
