// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The single-writer body drains a channel whose payload carries the IO row.
// The foreground context admits no effect, so mint_swmr_stage refuses the
// call, although each handle names its parameter.
//
// Expected diagnostic: CtxFitsSwmrPublishStage is not satisfied.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Stage.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Row.h>

#include <utility>

namespace swmr_row_fixture {
namespace c = ::fixy::concurrent;
namespace eff = ::foundation::effects;

// The fixture builds no channel, so one named brand stands in for the
// brand of a root site.
struct IoTag {};
struct FixtureBrand {};
using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using IoIn = c::PermissionedSpscChannel<IoPayload, 8, IoTag, FixtureBrand>;
struct Writer {
    void publish(IoPayload const&) noexcept {}
};
inline void body(IoIn::ConsumerHandle&&, Writer&&) noexcept {}

using Refused = decltype(c::mint_swmr_stage<&body>(std::declval<fixy::HotFgCtx const&>(),
                                                   std::declval<IoIn::ConsumerHandle>(), std::declval<Writer>()));
}  // namespace swmr_row_fixture

int main() { return 0; }
