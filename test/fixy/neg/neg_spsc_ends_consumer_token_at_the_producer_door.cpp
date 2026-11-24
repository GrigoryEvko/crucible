// The holder of the consumer token opens the producer side with it.  The two
// sides would then have one holder, and the thread of the consumer would
// write the producer side.  The producer door takes only the producer token.

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

}  // namespace

int main() {
    auto minted = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    with_producer(minted.split, minted.consumer_token, [](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {});
    return 0;
}
