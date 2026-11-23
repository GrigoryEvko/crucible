// A frame door owns its body and calls it one time through the once door
// call_once of foundation/Linear.h.  A named body keeps a second name that
// could call it again, so the named twin of the door refuses it with its
// reason.

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
    auto const body = [](auto producer) noexcept { std::move(producer).close(); };
    c::with_producer(channel, std::move(tokens.at(::foundation::core::index<0>())), body);
    return 0;
}
