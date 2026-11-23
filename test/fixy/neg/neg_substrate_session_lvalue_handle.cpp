// Negative compile test.  This file must fail to compile.
//
// The session owns the channel handle, so the mint takes it by move.  A
// handle passed as an lvalue would leave the caller and the session both
// acting on one channel, so mint_substrate_session does not bind to it.  The
// mint takes a forwarding reference, so an lvalue deduces a reference type,
// and a reference is no frame handle of the end of the direction.
//
// Expected diagnostic: no overload of mint_substrate_session takes an
// lvalue handle.

#include <fixy/channels/SubstrateSessionBridge.h>

#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace substrate_lvalue_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
namespace eff = ::foundation::effects;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<int, 8, decltype(channel_root())>;
}  // namespace substrate_lvalue_fixture

int main() {
    using namespace substrate_lvalue_fixture;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    c::with_producer(channel, std::move(tokens.at(::foundation::core::index<0>())), [&ctx](auto producer) noexcept {
        auto head = channels::mint_substrate_session<Channel, channels::Direction::Producer>(ctx, producer);
        std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
        ::foundation::linear::drop(std::move(producer).release());
    });
    return 0;
}
