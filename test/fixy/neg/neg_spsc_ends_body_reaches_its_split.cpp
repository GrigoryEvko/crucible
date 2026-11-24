// A body of the producer door captures the split and the producer token, and
// gives them to a helper, which opens the producer side again with that
// token: the route of a second frame of one side through a callee.  The body
// would hold two writers of one end.  The gate of the door refuses a body
// that reaches the token of its side (ReachesNoSideToken).
//
// The same body may open the consumer side with the consumer token.  The two
// ends of one split then open on one thread, as with_ends opens them, and the
// two frames name two ends, so that route is no defect.

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
void open_producer(Split& split, Token& token) noexcept {
    with_producer(split, token, [](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {});
}

}  // namespace

int main() {
    auto minted = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    auto& split = minted.split;
    auto& producer_token = minted.producer_token;
    with_producer(split, producer_token, [&split, &producer_token](::fixy::concurrent::ProducerEnd<Owner>&) noexcept {
        open_producer(split, producer_token);
    });
    return 0;
}
