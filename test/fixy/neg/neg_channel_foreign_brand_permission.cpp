// A channel has the brand of the root site that its permissions grow
// from.  Two root sites of one user tag give two brands, so the two
// channels here have one value type, one capacity and one tag, and they
// are still two types.  A producer token of the second site does not
// open the channel of the first site.

#include <fixy/concurrent/PermissionedSpscChannel.h>

#include <utility>

namespace c = fixy::concurrent;
namespace perm = foundation::permissions;

namespace {
struct TraceTag {};
auto first_root() noexcept { return perm::mint_permission_root<c::spsc_tag::Whole<TraceTag>>(); }
auto second_root() noexcept { return perm::mint_permission_root<c::spsc_tag::Whole<TraceTag>>(); }
}  // namespace

using FirstChannel = c::spsc_channel_t<int, 8, decltype(first_root())>;
using SecondChannel = c::spsc_channel_t<int, 8, decltype(second_root())>;

int main() {
    FirstChannel first{};
    auto [second_producer, second_consumer] =
        perm::mint_permission_split<SecondChannel::producer_tag, SecondChannel::consumer_tag>(second_root());
    (void)second_consumer;

    auto wrong = first.producer(std::move(second_producer));
    return wrong.try_push(1) ? 0 : 1;
}
