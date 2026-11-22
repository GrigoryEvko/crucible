// A body of the producer door captures the moved producer key and the split,
// and opens a second frame of the producer side with it.  The two bodies
// would hold the one producer end two times.  The door refuses a body that
// reaches the key of its side.

#include <fixy/concurrent/SpscEnds.h>

#include <foundation/effects/Effect.h>

#include <utility>

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
    auto& split = minted.split;
    auto& key = minted.producer_key;
    key = split.with_producer(std::move(key), [&split, &key](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {
        split.with_producer(key, [](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {});
    });
    return 0;
}
