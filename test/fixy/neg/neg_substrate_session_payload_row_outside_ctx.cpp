// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The channel carries a computation that needs IO.  The foreground context
// holds no effect.  The session of a channel handle must admit each effect
// that its payload carries, so mint_substrate_session rejects the call.
//
// Expected diagnostic: CtxFitsSubstrateSessionMint is not satisfied.

#include <fixy/concurrent/SubstrateSessionBridge.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace substrate_row_fixture {
namespace c = ::fixy::concurrent;
namespace eff = ::foundation::effects;
struct Tag {};
using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using Channel = c::PermissionedSpscChannel<IoWork, 8, Tag>;
}  // namespace substrate_row_fixture

int main() {
    using namespace substrate_row_fixture;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto [producer_perm, consumer_perm] = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(
        perm::mint_permission_root<Channel::whole_tag>());
    (void)consumer_perm;
    auto head =
        c::mint_substrate_session<Channel, c::Direction::Producer>(ctx, channel.producer(std::move(producer_perm)));
    std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
