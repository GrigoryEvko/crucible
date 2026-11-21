// The ends hold a Ref into the Box of their owner.  An owner that moves could
// leave its Box, and the Refs would then name a moved-from object, so the
// mint refuses an owner that moves.

#include <fixy/concurrent/SpscEnds.h>

#include <foundation/effects/Effect.h>

namespace {

struct MovableOwner final {
    struct producer_state {};
    struct consumer_state {};
};

}  // namespace

int main() {
    auto split = ::fixy::concurrent::mint_spsc_split<MovableOwner>(::foundation::effects::testing::test().alloc);
    return 0;
}
