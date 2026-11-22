// A drain of an MPSC channel gives each item to its drop in the order of the
// merge.  A drop with state could keep that order, so the drop holds no
// state: its class is empty.  A lambda that captures a counter is not empty.
//
// Expected diagnostic: drain has no candidate, because the class of the drop
// is not empty.

#include <fixy/concurrent/PermissionedChannel.h>

namespace {

struct UserTag {};
struct UserBrand {};

using Channel =
    ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::merge, int, 8, UserTag, UserBrand>;

[[maybe_unused]] int attempt(Channel& channel) noexcept {
    int count = 0;
    Channel::DrainClaim claim{channel};
    channel.drain(claim, [&count](int) noexcept { ++count; });
    return count;
}

}  // namespace

int main() { return 0; }
