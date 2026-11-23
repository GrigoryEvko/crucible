// Plant 08 of the audit of the threads for a pooled form: an Option of the
// outer scope keeps a producer end of a merge past the frame of its door, so
// the end, and its share of the pool, would outlive the channel.  The handle
// derives from its end privately, so the Option of the end takes no handle.

#include <fixy/Core.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace c = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

namespace {
struct LogTag {};
auto log_root() noexcept { return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, LogTag>>(); }
}  // namespace

using Channel = c::channel_t<int, 8, decltype(log_root())>;

int main() {
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(log_root());
    ::fixy::Option<Channel::ProducerEnd> kept{};
    {
        Channel channel{std::move(tokens.at(::foundation::core::index<0>()))};
        auto const lent = c::with_producer(channel, ::foundation::effects::testing::test().nondet,
                                           [&kept](auto producer) noexcept { kept.fill(std::move(producer)); });
        static_cast<void>(lent.is_some());
    }
    return kept.is_some() ? 1 : 0;
}
