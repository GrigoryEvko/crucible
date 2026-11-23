// A caller outside the frame doors opens the producer end of a wire with its
// token alone, and would keep the end past the channel.  Each claim door of a
// channel takes the key of the frame doors, and no code outside them builds
// one, so the call finds no door.

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
    auto end = channel.producer(std::move(tokens.at(::foundation::core::index<0>())));
    return end.try_push(7) ? 0 : 1;
}
