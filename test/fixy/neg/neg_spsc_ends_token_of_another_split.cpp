// Two mints give two splits of one owner type, and the producer token of the
// first opens the producer side of the second.  The second side would then
// have two holders.  Each mint has a brand of its own, and a token is a
// member class of the split of its brand, so it opens no other split.

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
