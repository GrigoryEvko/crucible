// A body of the door of the producer side of a held split gives a Ref of its
// end out as its result, so the end would stay reachable after the frame.
// The result of the door of a side holds no reference and no address.

#include <fixy/Core.h>
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
    auto const kept = split.producer_side().with_end(
        ProofDoor::producer(), [](::fixy::concurrent::ProducerEnd<Owner>& end) noexcept { return ::fixy::to(end); });
    static_cast<void>(kept);
    return 0;
}
