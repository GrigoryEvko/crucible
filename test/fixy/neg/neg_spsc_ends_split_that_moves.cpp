// The Box of the owner moves out of the split and ends in an inner scope,
// and an end then steps the freed owner.  The ends live beside the Box in
// the split, and the split does not move, so no code takes the Box out.

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
    [[maybe_unused]] auto moved = std::move(minted.split);
    return 0;
}
