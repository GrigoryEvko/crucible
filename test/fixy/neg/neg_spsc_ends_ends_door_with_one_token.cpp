// A caller that holds only the producer token opens the two ends of a split
// with that token in the place of the consumer token.  The holder of the
// consumer token could then write the consumer end at the same time.  The
// door of the two ends takes the token of each side.

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
    with_ends(minted.split, minted.producer_token, minted.producer_token,
              [](::fixy::concurrent::ProducerEnd<Owner>&, ::fixy::concurrent::ConsumerEnd<Owner>&) noexcept {});
    return 0;
}
