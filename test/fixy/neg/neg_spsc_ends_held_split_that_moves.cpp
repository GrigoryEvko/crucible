// A holder moves its held split.  The sides of a held split hold a Ref into
// the Box of its owner, and a consumer can hold a reference to a side, so a
// move would leave that reference on the old object.  A held split does not
// move.

#include <fixy/concurrent/SpscEnds.h>

#include <foundation/effects/Effect.h>

#include <utility>

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
struct HeldBrand final {
    using producer_proof = ProducerProof;
    using consumer_proof = ConsumerProof;
};

}  // namespace

int main() {
    auto split =
        ::fixy::concurrent::mint_held_spsc_split<Owner, HeldBrand>(::foundation::effects::testing::test().alloc);
    [[maybe_unused]] auto moved = std::move(split);
    return 0;
}
