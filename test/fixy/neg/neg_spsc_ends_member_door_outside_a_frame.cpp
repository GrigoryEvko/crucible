// A caller that holds the producer token takes the producer end from the
// member door of the split, outside each frame door, and keeps it.  The end
// would then outlive the frame, and a later frame of the token would give a
// second writer of the side.  The member door takes the key of the frame
// doors, which only the frame doors build.

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
    ::fixy::concurrent::SpscFrameKey const key{};
    [[maybe_unused]] auto& kept = minted.split.producer_end_(key, minted.producer_token);
    return 0;
}
