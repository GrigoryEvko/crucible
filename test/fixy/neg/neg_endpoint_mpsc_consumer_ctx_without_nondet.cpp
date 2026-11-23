// An endpoint of the consumer of an MPSC channel pops a merge, so the
// context of the endpoint holds Nondet.  The foreground context holds no
// atom, and the mint of the endpoint refuses it.  The endpoint would
// otherwise give its pop the tag of a context that cannot hold it.  The
// handle is the frame handle of the consumer, so the mint stands in the body
// of the frame door.

#include <fixy/channels/Endpoint.h>
#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace c = ::fixy::concurrent;
namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

namespace {

struct UserTag {};
auto channel_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, UserTag>>();
}

using Channel = c::channel_t<int, 8, decltype(channel_root())>;
using FgCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;

}  // namespace

int main() {
    const FgCtx ctx = eff::testing::foreground();
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    Channel channel{std::move(tokens.at(::foundation::core::index<0>()))};
    c::with_consumer(channel, std::move(tokens.at(::foundation::core::index<1>())), [&ctx](auto consumer) noexcept {
        auto endpoint =
            fixy::channels::mint_endpoint<Channel, fixy::channels::Direction::Consumer>(ctx, std::move(consumer));
        static_cast<void>(endpoint);
    });
    return 0;
}
