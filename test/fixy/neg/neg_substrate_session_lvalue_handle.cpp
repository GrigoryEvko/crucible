// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The session owns the channel handle, so the mint takes it by move.  A
// handle passed as an lvalue would leave the caller and the session both
// acting on one channel, so mint_substrate_session does not bind to it.
//
// Expected diagnostic: no overload of mint_substrate_session takes an
// lvalue handle.

#include <fixy/concurrent/SubstrateSessionBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace substrate_lvalue_fixture {
namespace c = ::fixy::concurrent;
namespace eff = ::foundation::effects;
struct Tag {};
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::spsc_tag::Whole<Tag>>();
}
using Channel = c::spsc_channel_t<int, 8, decltype(channel_root())>;
}  // namespace substrate_lvalue_fixture

int main() {
    using namespace substrate_lvalue_fixture;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    (void)consumer_perm;
    auto producer = channel.producer(std::move(producer_perm));
    auto head = c::mint_substrate_session<Channel, c::Direction::Producer>(ctx, producer);
    std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
