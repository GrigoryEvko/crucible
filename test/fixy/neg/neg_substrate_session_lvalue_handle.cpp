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
using Channel = c::PermissionedSpscChannel<int, 8, Tag>;
}  // namespace substrate_lvalue_fixture

int main() {
    using namespace substrate_lvalue_fixture;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto [producer_perm, consumer_perm] = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(
        perm::mint_permission_root<Channel::whole_tag>());
    (void)consumer_perm;
    auto producer = channel.producer(std::move(producer_perm));
    auto head = c::mint_substrate_session<Channel, c::Direction::Producer>(ctx, producer);
    std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
