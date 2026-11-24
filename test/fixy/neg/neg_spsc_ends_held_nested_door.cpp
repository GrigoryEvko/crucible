// A body of the door of the producer side of a held split borrows the split
// and the proof, and opens a second frame of the producer side.  The two
// bodies would hold the one producer end two times.  The door of a side
// refuses a body that reaches a side of its proof through a borrow, also
// through the held split that holds it.

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

public:
    ConsumerProof(ConsumerProof const&) = delete("a proof lives for its call");
    ~ConsumerProof() = default;
};
struct ProofDoor final {
    static ProducerProof producer() noexcept { return ProducerProof{}; }
};
struct HeldBrand final {
    using producer_proof = ProducerProof;
    using consumer_proof = ConsumerProof;
};

}  // namespace

int main() {
    auto split =
        ::fixy::concurrent::mint_held_spsc_split<Owner, HeldBrand>(::foundation::effects::testing::test().alloc);
    ProducerProof const proof = ProofDoor::producer();
    split.producer_side().with_end(proof, [&split, &proof](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {
        split.producer_side().with_end(proof, [](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {});
    });
    return 0;
}
