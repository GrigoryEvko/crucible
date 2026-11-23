// Negative compile test.  This file must fail to compile.
//
// The claim of a drain of a merge channel takes the lease of the producer
// root, which proves that no producer handle lives and that the drain sees
// each published item.  A claim with no lease finds no door.
//
// Expected diagnostic: claim_drain() has an unsatisfied constraint on a merge
// channel, and the other candidate takes the lease.

#include <fixy/concurrent/PermissionedChannel.h>

namespace {

struct UserTag {};
struct UserBrand {};

using Channel =
    ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::merge, int, 8, UserTag, UserBrand>;

[[maybe_unused]] void attempt(Channel& channel) noexcept {
    Channel::DrainClaim claim = channel.claim_drain();
    channel.drain(claim, [](int) noexcept {});
}

}  // namespace

int main() { return 0; }
