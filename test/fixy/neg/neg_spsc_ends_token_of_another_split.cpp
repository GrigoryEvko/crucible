// Two mint sites give two splits of one owner type, and the producer token of
// the first is given to the producer side of the second.  The second side
// would then have two holders.  Each site has a brand of its own, and a token
// is a member class of the split of its brand, so it does not fit the door of
// the other split.  Two runs of one site share a brand, so the door compares
// the Identity of the token with the Identity of the split there
// (test_spsc_ends.cpp).

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
    auto first = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    auto second = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    with_producer(second.split, first.producer_token, [](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {});
    return 0;
}
