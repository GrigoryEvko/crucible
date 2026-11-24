// A caller takes the view of the owner from an rvalue of the consumer side.
// The view lives as long as the held split that holds the side, and only a
// side that the split holds gives one, so the rvalue twin is deleted.

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
    auto const view = std::move(split.consumer_side()).owner_view();
    static_cast<void>(view);
    return 0;
}
