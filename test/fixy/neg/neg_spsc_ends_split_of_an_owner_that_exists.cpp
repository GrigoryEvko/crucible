// An owner splits into its two ends only at its mint, which builds the owner.
// No door takes an owner that exists, so a second split of one owner does not
// compile: the mint refuses an argument that does not build a new owner.

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
    auto split = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    auto again = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc, *split.owner);
    return 0;
}
