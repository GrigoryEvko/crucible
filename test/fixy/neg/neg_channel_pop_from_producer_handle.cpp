// The role lives in the handle type.  A producer handle exposes
// try_push and the snapshots; it has no try_pop, so draining a channel
// from the producing side is not something a call site can express.
//
// The permission is the right one and the channel is well formed, so
// the only thing missing is the member.

#include <fixy/concurrent/PermissionedSpscChannel.h>

#include <utility>

namespace c = fixy::concurrent;
namespace perm = foundation::permissions;

namespace {
struct TraceTag {};
}  // namespace

using Channel = c::PermissionedSpscChannel<int, 8, TraceTag>;

int main() {
    Channel channel{};
    auto whole = perm::mint_permission_root<Channel::whole_tag>();
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(whole));
    (void)consumer_perm;

    auto producer = channel.producer(std::move(producer_perm));
    return producer.try_pop().has_value() ? 0 : 1;
}
