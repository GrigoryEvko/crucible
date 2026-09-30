// A ProducerHandle owns the channel's linear Producer Permission.
// Copying it would leave two handles each believing it is the one
// producer, and the SPSC ring's head is written by exactly one thread
// with no synchronization against a second writer.
//
// The consumer side and the channel itself are untouched here, so the
// only thing that can refuse this is the deleted copy constructor.

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
    auto second = producer;
    return second.try_push(1) ? 0 : 1;
}
