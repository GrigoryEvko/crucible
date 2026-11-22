// A body of the producer door calls a closure of its own text, and the
// closure opens a second frame of the producer side with the key.  The
// closure takes the key from the body, so the body captures the key, and the
// door refuses it.

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
        [&split, &key]() noexcept {
            split.with_producer(key, [](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {});
        }();
    });
    return 0;
}
