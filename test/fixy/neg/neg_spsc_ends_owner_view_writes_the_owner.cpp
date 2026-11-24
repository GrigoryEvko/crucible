// A caller writes the owner of a held split through the view of the consumer
// side.  The view names the owner as const, so each write goes through a door
// that takes an end, and the write through the view does not compile.

#include <fixy/concurrent/SpscEnds.h>

#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

struct Owner final {
    struct producer_state {};
    struct consumer_state {};
    Owner() noexcept = default;
    Owner(Owner const&) = delete("pinned");
    Owner(Owner&&) = delete("pinned");
    std::uint64_t count = 0;
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
    auto const view = split.consumer_side().owner_view();
    view->count = 1;
    return 0;
}
