// Negative compile test.  This file must fail to compile.
//
// The fan-in body drains a channel whose payload carries the IO row.  The
// foreground context admits no effect, so mint_mpmc_stage refuses the call,
// although each end names its parameter.
//
// Expected diagnostic: CtxFitsVariadicStage is not satisfied.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/concurrent/Stage.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Row.h>

#include <utility>

namespace mpmc_row_fixture {
namespace c = ::fixy::concurrent;
namespace eff = ::foundation::effects;

// The fixture builds no channel, so one named brand stands in for the
// brand of a root site.  The tags keep the three channels apart.  The row
// of the stage is a fact of the types, so the body takes the ends of the
// channels, and no frame door opens a handle.
struct IoTag {};
struct PlainTag {};
struct OutTag {};
struct FixtureBrand {};
using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using IoIn = c::PermissionedChannel<c::ChannelForm::wire, IoPayload, 8, IoTag, FixtureBrand>;
using PlainIn = c::PermissionedChannel<c::ChannelForm::wire, int, 8, PlainTag, FixtureBrand>;
using Out = c::PermissionedChannel<c::ChannelForm::wire, int, 8, OutTag, FixtureBrand>;
inline void fan_in(IoIn::ConsumerEnd&&, PlainIn::ConsumerEnd&&, Out::ProducerEnd&&) noexcept {}

using Refused =
    decltype(c::mint_mpmc_stage<&fan_in>(std::declval<fixy::HotFgCtx const&>(), std::declval<IoIn::ConsumerEnd>(),
                                         std::declval<PlainIn::ConsumerEnd>(), std::declval<Out::ProducerEnd>()));
}  // namespace mpmc_row_fixture

int main() { return 0; }
