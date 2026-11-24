// A body of the producer door captures the moved producer token and the
// split, and opens a second frame of the producer side with it.  The two
// bodies would hold the one producer end two times.  The door refuses a body
// that reaches the token of its side.

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
        with_producer(split, token, [](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {});
    });
    return 0;
}
