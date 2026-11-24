// A caller builds a second producer token of a split.  The side would then
// have two holders.  The constructor of a token is private, and the door of
// the mint is its one friend, so only mint_spsc_split builds a token.

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
    using Brand = decltype(minted.split)::brand_type;
    [[maybe_unused]] ::fixy::concurrent::ProducerToken<Owner, Brand> second{};
    return 0;
}
