// A body of the pair door copies its pair.  A copy could outlive the frame of
// the door and name the two ends after the frame, beside the next holder of
// each side.  The copy of a pair is deleted.

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
    with_end_pair(minted.split, minted.producer_token, minted.consumer_token,
                  [](::fixy::concurrent::SpscEndPair<Owner>& ends) noexcept {
                      ::fixy::concurrent::SpscEndPair<Owner> copy{ends};
                      static_cast<void>(copy);
                  });
    return 0;
}
