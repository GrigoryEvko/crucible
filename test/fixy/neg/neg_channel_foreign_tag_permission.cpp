// Each channel needs a UserTag of its own.  Two channels sharing one
// tag share Permission types, and their endpoints become
// interchangeable: a producer token minted for one would open the
// other, and each channel would then have two producers with nothing
// to say so.
//
// Both channels here carry the same value type and the same capacity,
// so only the tag separates them.  The token is minted correctly for
// its own channel; passing it to the other one is what fails.

#include <fixy/concurrent/PermissionedSpscChannel.h>

#include <utility>

namespace c = fixy::concurrent;
namespace perm = foundation::permissions;

namespace {
struct TraceTag {};
struct MetaTag {};
}  // namespace

using TraceChannel = c::PermissionedSpscChannel<int, 8, TraceTag>;
using MetaChannel = c::PermissionedSpscChannel<int, 8, MetaTag>;

int main() {
    MetaChannel meta{};
    auto whole = perm::mint_permission_root<TraceChannel::whole_tag>();
    auto [trace_producer, trace_consumer] =
        perm::mint_permission_split<TraceChannel::producer_tag, TraceChannel::consumer_tag>(std::move(whole));
    (void)trace_consumer;

    auto wrong = meta.producer(std::move(trace_producer));
    return wrong.try_push(1) ? 0 : 1;
}
