// A body of the door of two held sides reaches the held split of its first
// side through the holder, and opens a second frame of that side inside the
// frame of the door.  The body would hold two writers of one end.  The door
// refuses a body that reaches a side of either proof through a borrow.

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

// The holder of a held split.  It lends its split to each caller that names
// it.
struct Holder final {
    ::fixy::concurrent::SpscHeldSplit<Owner, HeldBrand> split{
        ::fixy::concurrent::mint_held_spsc_split<Owner, HeldBrand>(::foundation::effects::testing::test().alloc)};
};

}  // namespace

int main() {
    Holder holder{};
    ProducerProof const producer = ProofDoor::producer();
    ConsumerProof const consumer = ProofDoor::consumer();
    with_held_ends(holder.split.producer_side(), producer, holder.split.consumer_side(), consumer,
                   [&holder, &producer](::fixy::concurrent::ProducerEnd<Owner>&,
                                        ::fixy::concurrent::ConsumerEnd<Owner>&) noexcept {
                       holder.split.producer_side().with_end(producer,
                                                             [](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {});
                   });
    return 0;
}
