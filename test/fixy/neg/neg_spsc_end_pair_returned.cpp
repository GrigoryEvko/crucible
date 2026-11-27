// A body of the pair door gives a Ref of its pair as its result.  The result
// leaves the frame, so the caller could write the two sides after the frame,
// with no token and beside the next holder of each side.  The result of a
// body of the pair door holds no borrow, so the door refuses the body.

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
    auto kept = with_end_pair(
        minted.split, minted.producer_token, minted.consumer_token,
        [](::fixy::concurrent::SpscEndPair<Owner>& ends) noexcept { return ::fixy::erase_brand(::fixy::to(ends)); });
    static_cast<void>(kept);
    return 0;
}
