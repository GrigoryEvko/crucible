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
}  // namespace

using Channel = c::PermissionedSpscChannel<int, 8, TraceTag>;

int main() {
    Channel channel{};
    auto whole = perm::mint_permission_root<Channel::whole_tag>();
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(whole));
    (void)consumer_perm;

    auto producer = channel.producer(std::move(producer_perm));
    auto second = producer;
    return second.try_push(1) ? 0 : 1;
}
