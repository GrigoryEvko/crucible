// A body of the pair door keeps a Ref of its pair in a variable that it
// captures by reference.  The Ref outlives the frame, so the caller could
// write the two sides after the frame, with no token and beside the next
// holder of each side.  The store check of the door (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h) refuses the body.

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
    ::fixy::Option<::fixy::Ref<::fixy::concurrent::SpscEndPair<Owner>>> kept{::fixy::none};
    with_end_pair(minted.split, minted.producer_token, minted.consumer_token,
                  [&kept](::fixy::concurrent::SpscEndPair<Owner>& ends) noexcept {
                      kept = ::fixy::some(::fixy::erase_brand(::fixy::to(ends)));
                  });
    return kept.is_some() ? 0 : 1;
}
