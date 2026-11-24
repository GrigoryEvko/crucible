// A caller takes the consumer side of a held split that ends at the end of
// the statement, and keeps the reference.  The side ends with the split, so
// the reference would name a side that ended.  A temporary held split lends
// no side.

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
    [[maybe_unused]] auto& side =
        ::fixy::concurrent::mint_held_spsc_split<Owner, HeldBrand>(::foundation::effects::testing::test().alloc)
            .consumer_side();
    return 0;
}
