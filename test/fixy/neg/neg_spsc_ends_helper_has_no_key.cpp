// A helper takes the split, and no token, and opens the producer side.  A
// split lends an end only with the token of its side, and the helper has
// none.
//
// The helper runs outside a frame, because no body of a door of the split
// reaches it inside a frame.  The owner holds a plain count, so the split
// stays on one thread, and a capture of the split by a reference that is not
// const, or of a Ref or the address of the split by value, fails the store
// check of the door (StoresNoBorrowOutside of foundation/core/CaptureStore.h),
// because the split holds the borrows of its ends
// (neg_spsc_ends_body_reaches_its_split).  A capture by a const reference
// reaches no door, because each door takes a split that is not const.  A
// split in static storage fails the check loan-key-static of the quarantine
// plugin.  So the missing token is the one refusal here.

#include <fixy/concurrent/SpscEnds.h>

#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

struct Owner final {
    struct producer_state {};
    struct consumer_state {};
    Owner() noexcept = default;
    Owner(Owner const&) = delete("pinned");
    Owner(Owner&&) = delete("pinned");
    std::uint64_t count = 0;
};

template <class Split>
void open_without_token(Split& split) noexcept {
    with_producer(split, [](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {});
}

}  // namespace

int main() {
    auto minted = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    open_without_token(minted.split);
    return 0;
}
