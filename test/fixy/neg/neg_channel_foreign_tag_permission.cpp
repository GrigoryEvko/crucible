// Two channels of two user tags have two sets of Permission types.  A
// producer token of one channel does not open the other, so neither
// channel gets a second producer.
//
// The two channels here carry the same value type and the same capacity.
// The fixture splits the token correctly for its own channel.  The call
// that gives the token to the other channel fails.

#include <fixy/concurrent/PermissionedSpscChannel.h>

#include <utility>

namespace c = fixy::concurrent;
namespace perm = foundation::permissions;

namespace {
struct TraceTag {};
struct MetaTag {};
auto trace_root() noexcept { return perm::mint_permission_root<c::spsc_tag::Whole<TraceTag>>(); }
auto meta_root() noexcept { return perm::mint_permission_root<c::spsc_tag::Whole<MetaTag>>(); }
}  // namespace

using TraceChannel = c::spsc_channel_t<int, 8, decltype(trace_root())>;
using MetaChannel = c::spsc_channel_t<int, 8, decltype(meta_root())>;

int main() {
    MetaChannel meta{};
    auto [trace_producer, trace_consumer] =
        perm::mint_permission_split<TraceChannel::producer_tag, TraceChannel::consumer_tag>(trace_root());
    (void)trace_consumer;

    auto wrong = meta.producer(std::move(trace_producer));
    return wrong.try_push(1) ? 0 : 1;
}
