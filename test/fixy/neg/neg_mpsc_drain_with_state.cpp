// Negative compile test.  This file must fail to compile.
//
// A drain of an MPSC channel gives each item to its drop in the order of the
// merge.  A drop with state could keep that order, so the drop holds no
// state: its class is empty.  A lambda that captures a counter is not empty.
// The claim of the drain takes the lease of the producer root, so only the
// drop fails.
//
// Expected diagnostic: drain has no candidate, because the class of the drop
// is not empty, so the drop of a merge fails ChannelDrainDrop.

#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace {

struct UserTag {};
struct UserBrand {};

using Channel =
    ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::merge, int, 8, UserTag, UserBrand>;

[[maybe_unused]] int attempt(Channel& channel) noexcept {
    int count = 0;
    auto root = channel.try_withdraw_producer_root(::foundation::effects::testing::test().nondet)
                    .expect("the root comes out with no producer out");
    {
        Channel::DrainClaim claim = channel.claim_drain(root);
        channel.drain(claim, [&count](int) noexcept { ++count; });
    }
    channel.deposit_producer_root(std::move(root));
    return count;
}

}  // namespace

int main() { return 0; }
