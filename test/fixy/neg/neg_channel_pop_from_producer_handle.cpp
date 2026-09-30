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
auto trace_root() noexcept { return perm::mint_permission_root<c::spsc_tag::Whole<TraceTag>>(); }
}  // namespace

using Channel = c::spsc_channel_t<int, 8, decltype(trace_root())>;

int main() {
    Channel channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(trace_root());
    (void)consumer_perm;

    auto producer = channel.producer(std::move(producer_perm));
    return producer.try_pop().has_value() ? 0 : 1;
}
