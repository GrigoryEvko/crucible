// Negative compile test.  This file must fail to compile.
//
// The single-writer body drains a channel whose payload carries the IO row.
// The foreground context admits no effect, so mint_swmr_stage refuses the
// call, although each argument names its parameter.
//
// Expected diagnostic: CtxFitsSwmrPublishStage is not satisfied.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/concurrent/Stage.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Row.h>

#include <utility>

namespace swmr_row_fixture {
namespace c = ::fixy::concurrent;
namespace eff = ::foundation::effects;

// The fixture builds no channel, so one named brand stands in for the
// brand of a root site.  The row of the stage is a fact of the types, so
// the body takes the consumer end of the channel, and no frame door opens a
// handle.
struct IoTag {};
struct FixtureBrand {};
using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using IoIn = c::PermissionedChannel<c::ChannelForm::wire, IoPayload, 8, IoTag, FixtureBrand>;
struct Writer {
    void publish(IoPayload const&) noexcept {}
};
inline void body(IoIn::ConsumerEnd&&, Writer&&) noexcept {}

using Refused = decltype(c::mint_swmr_stage<&body>(std::declval<fixy::HotFgCtx const&>(),
                                                   std::declval<IoIn::ConsumerEnd>(), std::declval<Writer>()));
}  // namespace swmr_row_fixture

int main() { return 0; }
