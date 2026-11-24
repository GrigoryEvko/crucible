// A helper takes the split, and no key, and opens the producer side.  A split
// lends an end only with the key of its side, and the helper has none.
//
// The helper runs outside a frame, because no body of a door of the split
// reaches it inside a frame.  A capture of the split by a reference that is
// not const, or of a Ref or the address of the split by value, fails the
// store check of the door (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h), because the split holds the borrows of its
// ends (neg_spsc_ends_body_reaches_its_split).  A capture by a const
// reference reaches no door, because each door takes a split that is not
// const.  A split in static storage fails the check loan-key-static of the
// quarantine plugin.  So the key is the one refusal here.

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

template <class Split>
void open_without_key(Split& split) noexcept {
    split.with_producer([](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {});
}

}  // namespace

int main() {
    auto minted = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    open_without_key(minted.split);
    return 0;
}
