// A body of the pair door moves its pair into a new object.  The new object
// could outlive the frame of the door, as a copy could.  A pair declares no
// move, so the move takes the deleted copy.

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

}  // namespace

int main() {
    auto minted = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    with_end_pair(minted.split, minted.producer_token, minted.consumer_token,
                  [](::fixy::concurrent::SpscEndPair<Owner>& ends) noexcept {
                      ::fixy::concurrent::SpscEndPair<Owner> moved{std::move(ends)};
                      static_cast<void>(moved);
                  });
    return 0;
}
