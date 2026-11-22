// A helper takes an end by a reference and moves it out, as a hand-over to a
// holder does.  The caller would keep a working end, and the owner would have
// two ends of one side.  An end lives in its split and does not move.

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

::fixy::concurrent::ProducerEnd<Owner> hand_over(::fixy::concurrent::ProducerEnd<Owner>& end) noexcept {
    return std::move(end);
}

}  // namespace

int main() {
    auto minted = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    minted.split.with_producer(minted.producer_key, [](::fixy::concurrent::ProducerEnd<Owner>& end) noexcept {
        static_cast<void>(hand_over(end));
    });
    return 0;
}
