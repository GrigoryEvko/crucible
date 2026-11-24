// An end of an SPSC owner is the one holder of its side.  A copy would give a
// second producer of the owner, so the copy is deleted.

#include <fixy/concurrent/SpscEnds.h>

#include <foundation/effects/Effect.h>

namespace {

struct Owner final {
    struct producer_state {};
    struct consumer_state {};
    Owner() noexcept = default;
    Owner(Owner&&) = delete("pinned");
};

}  // namespace

int main() {
    auto minted = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    with_producer(minted.split, minted.producer_token,
                  [](::fixy::concurrent::ProducerEnd<Owner>& end) noexcept { [[maybe_unused]] auto second = end; });
    return 0;
}
