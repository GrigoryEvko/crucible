// A body of the producer door gives a Ref of its end out as its result, so
// the end would stay reachable after the frame.  The result of a frame door
// of a split holds no reference and no address.

#include <fixy/Core.h>
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

}  // namespace

int main() {
    auto minted = ::fixy::concurrent::mint_spsc_split<Owner>(::foundation::effects::testing::test().alloc);
    auto const kept = minted.split.with_producer(
        minted.producer_key, [](::fixy::concurrent::ProducerEnd<Owner>& end) noexcept { return ::fixy::to(end); });
    static_cast<void>(kept);
    return 0;
}
