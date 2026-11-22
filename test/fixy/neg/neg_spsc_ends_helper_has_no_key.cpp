// A helper takes the split, and no key, and opens the producer side.  A body
// of the producer door calls the helper, as the route of a second frame
// through a callee does.  A split lends an end only with the key of its side,
// and the helper has none.

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
    auto& split = minted.split;
    split.with_producer(minted.producer_key,
                        [&split](::fixy::concurrent::ProducerEnd<Owner>&) noexcept { open_without_key(split); });
    return 0;
}
