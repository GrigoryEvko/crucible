// A body of the producer door calls a closure of its own text, and the
// closure opens a second frame of the producer side with the token.  The
// closure takes the token from the body, so the body captures the token, and
// the door refuses it.

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
    auto& token = minted.producer_token;
    token = with_producer(split, std::move(token), [&split, &token](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {
        [&split, &token]() noexcept {
            with_producer(split, token, [](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {});
        }();
    });
    return 0;
}
