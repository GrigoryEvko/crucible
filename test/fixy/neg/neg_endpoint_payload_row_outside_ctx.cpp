// Negative compile test.  This file must fail to compile.
//
// The channel carries a computation that needs IO, and the foreground
// context holds no effect.  An endpoint promises that its session starts,
// so its mint asks the gate of that session, and mint_endpoint rejects the
// call.
//
// Expected diagnostic: CtxFitsEndpointMint is not satisfied.

#include <fixy/channels/Endpoint.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace endpoint_row_fixture {
namespace c = ::fixy::concurrent;
namespace channels = ::fixy::channels;
namespace eff = ::foundation::effects;
struct Tag {};
// The scope brand of a frame that no door opens.  It names the type of the
// frame handle of the producer.
struct FrameScope {};
using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
inline auto channel_root() noexcept {
    return ::foundation::permissions::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, Tag>>();
}
using Channel = c::channel_t<IoWork, 8, decltype(channel_root())>;
using Producer = Channel::producer_handle<FrameScope>;
}  // namespace endpoint_row_fixture

// The ring of the channel builds each slot from an empty initializer, and
// a computation has no such constructor, so no channel of this payload is
// built, and no frame door opens a handle of it.  The frame handle comes
// from declval in an unevaluated operand, and the size of the call is its
// one use, so the refused mint is the only error.
int main() {
    using namespace endpoint_row_fixture;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    static_cast<void>(
        sizeof(channels::mint_endpoint<Channel, channels::Direction::Producer>(ctx, std::declval<Producer>())));
    return 0;
}
