// A caller names the scope brand of a frame door, so it could spell the type
// of the handle outside the body and keep the handle there.  The pack before
// the brand takes each explicit template argument, and the door asks that the
// pack is empty, so the call finds no door.

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace c = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

namespace {
struct LogTag {};
auto log_root() noexcept { return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, LogTag>>(); }
struct NamedScope {};
}  // namespace

using Channel = c::channel_t<int, 8, decltype(log_root())>;

int main() {
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(log_root());
    Channel channel{};
    c::with_producer<NamedScope>(channel, std::move(tokens.at(::foundation::core::index<0>())),
                                 [](auto producer) noexcept { std::move(producer).close(); });
    return 0;
}
