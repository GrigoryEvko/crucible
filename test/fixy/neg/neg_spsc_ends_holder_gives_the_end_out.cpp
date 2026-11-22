// A holder of an end gives it out through a member with no qualifier &&, so
// each call would give one more working end of one side.  An end lives in
// its split and does not move.

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

struct ProducerStage final {
    ::fixy::concurrent::ProducerEnd<Owner>& end;

    [[nodiscard]] ::fixy::concurrent::ProducerEnd<Owner> give_end() noexcept { return std::move(end); }
};

}  // namespace

int main() {
    auto minted = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    minted.split.with_producer(minted.producer_key, [](::fixy::concurrent::ProducerEnd<Owner>& end) noexcept {
        ProducerStage stage{end};
        static_cast<void>(stage.give_end());
    });
    return 0;
}
