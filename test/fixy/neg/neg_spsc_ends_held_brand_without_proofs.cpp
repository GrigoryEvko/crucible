// A class mints a held split of a brand that names no proof of a side.  Such
// a split would open with no key and no proof, so the gate of the mint
// refuses the brand.

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

struct PlainBrand final {};

}  // namespace

int main() {
    [[maybe_unused]] auto split =
        ::fixy::concurrent::mint_held_spsc_split<Owner, PlainBrand>(::foundation::effects::testing::test().alloc);
    return 0;
}
