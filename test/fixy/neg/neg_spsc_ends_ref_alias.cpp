// A body of the producer door opens a second frame of the producer side
// through a Ref of the split.  A split lends an end only with the key of its
// side, and the Ref carries none.

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

}  // namespace

int main() {
    auto minted = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    auto const alias = ::fixy::to(minted.split);
    minted.split.with_producer(minted.producer_key, [&alias](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {
        (*alias).with_producer([](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {});
    });
    return 0;
}
