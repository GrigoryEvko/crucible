// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The channel carries a computation that needs IO, and the foreground
// context holds no effect.  An endpoint promises that its session starts,
// so its mint asks the gate of that session, and mint_endpoint rejects the
// call.
//
// Expected diagnostic: CtxFitsEndpointMint is not satisfied.

#include <fixy/concurrent/Endpoint.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace endpoint_row_fixture {
namespace c = ::fixy::concurrent;
namespace eff = ::foundation::effects;
struct Tag {};
using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::spsc_tag::Whole<Tag>>();
}
using Channel = c::spsc_channel_t<IoWork, 8, decltype(channel_root())>;
}  // namespace endpoint_row_fixture

int main() {
    using namespace endpoint_row_fixture;
    namespace perm = ::foundation::permissions;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Channel channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(channel_root());
    (void)consumer_perm;
    auto endpoint = c::mint_endpoint<Channel, c::Direction::Producer>(ctx, channel.producer(std::move(producer_perm)));
    (void)endpoint;
    return 0;
}
