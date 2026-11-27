// A body of the door of the two ends builds a pair of ends by hand.  A pair
// that any code builds could hold the ends of two owners, and a door of the
// owner that takes a pair, such as a reset, would then write two owners.  The
// constructor of a pair is private, and only the pair doors build one.

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
    with_ends(minted.split, minted.producer_token, minted.consumer_token,
              [](::fixy::concurrent::ProducerEnd<Owner>& producer,
                 ::fixy::concurrent::ConsumerEnd<Owner>& consumer) noexcept {
                  ::fixy::concurrent::SpscEndPair<Owner> pair{producer, consumer};
                  static_cast<void>(pair);
              });
    return 0;
}
