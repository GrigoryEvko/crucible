// A body of the producer door captures the split by a reference that is not
// const, and gives it to a helper, which opens the consumer side with its
// token: the route of a second frame through a callee.  The owner holds a
// plain count, so the split stays on one thread and holds the borrows of its
// ends, and the store check of the door (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h) refuses the capture.  A split of an owner
// that two threads write states synchronized{}, and its doors take the token
// of each side, so a capture of it opens no side without its token.

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

template <class Split, class Token>
void open_consumer(Split& split, Token& token) noexcept {
    with_consumer(split, token, [](::fixy::concurrent::ConsumerEnd<Owner>&) noexcept {});
}

}  // namespace

int main() {
    auto minted = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    auto& split = minted.split;
    auto& consumer_token = minted.consumer_token;
    with_producer(split, minted.producer_token,
                  [&split, &consumer_token](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {
                      open_consumer(split, consumer_token);
                  });
    return 0;
}
