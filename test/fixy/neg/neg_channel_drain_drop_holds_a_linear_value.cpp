// The drain of a channel borrows its drop and calls it for each item, and a
// call is no consume.  A drop that holds a linear token by value would end
// the token with the drop, where no rule reads the end.  The gate of the drain
// refuses such a drop.

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace c = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

namespace {
struct LogTag {};
auto log_root() noexcept { return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, LogTag>>(); }
}  // namespace

using Channel = c::channel_t<int, 8, decltype(log_root())>;

int main() {
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(log_root());
    Channel channel{};
    auto claim = channel.claim_drain();
    channel.drain(claim, [held = std::move(tokens.at(::foundation::core::index<0>()))](int /*item*/) noexcept {});
    return 0;
}
