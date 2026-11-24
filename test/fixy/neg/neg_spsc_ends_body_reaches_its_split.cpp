// A body of the producer door captures the split by a reference that is not
// const, and gives it to a helper, which opens the consumer side with its
// key: the route of a second frame through a callee.  The split holds the
// borrows of its ends, so the store check of the door (StoresNoBorrowOutside
// of foundation/core/CaptureStore.h) refuses the capture, and the route
// closes at the door.

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

template <class Split, class Key>
void open_consumer(Split& split, Key& key) noexcept {
    split.with_consumer(key, [](::fixy::concurrent::ConsumerEnd<Owner>&) noexcept {});
}

}  // namespace

int main() {
    auto minted = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    auto& split = minted.split;
    auto& consumer_key = minted.consumer_key;
    split.with_producer(minted.producer_key, [&split, &consumer_key](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {
        open_consumer(split, consumer_key);
    });
    return 0;
}
