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
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::spsc_tag::Whole<Tag>>();
}
using Channel = c::spsc_channel_t<IoWork, 8, decltype(channel_root())>;
}  // namespace substrate_row_fixture

// The ring of the channel builds each slot from an empty initializer, and
// a computation has no such constructor, so no channel of this payload is
// built.  The handle comes from declval in an unevaluated operand, and the
// size of the call is its one use, so the refused mint is the only error.
int main() {
    using namespace substrate_row_fixture;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    static_cast<void>(sizeof(
        c::mint_substrate_session<Channel, c::Direction::Producer>(ctx, std::declval<Channel::ProducerHandle>())));
    return 0;
}
