// Plant 08 of the audit of the threads, in the form of a frame door: an
// Option of the outer scope keeps the producer end past the frame of its
// door, so the end would outlive its channel, and a push would write the
// channel after its end.  The handle derives from its end privately, so the
// Option of the end takes no handle.

#include <fixy/Core.h>
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
    ::fixy::Option<Channel::ProducerEnd> kept{};
    {
        Channel channel{};
        c::with_producer(channel, std::move(tokens.at(::foundation::core::index<0>())),
                         [&kept](auto producer) noexcept { kept.fill(std::move(producer)); });
    }
    return kept.is_some() ? 1 : 0;
}
