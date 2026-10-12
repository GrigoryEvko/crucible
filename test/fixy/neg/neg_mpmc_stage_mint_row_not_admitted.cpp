// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The fan-in body drains a channel whose payload carries the IO row.  The
// foreground context admits no effect, so mint_mpmc_stage refuses the call,
// although each handle names its parameter.
//
// Expected diagnostic: CtxFitsVariadicStage is not satisfied.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Stage.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Row.h>

#include <utility>

namespace mpmc_row_fixture {
namespace c = ::fixy::concurrent;
namespace eff = ::foundation::effects;

// The fixture builds no channel, so one named brand stands in for the
// brand of a root site.  The tags keep the three channels apart.
struct IoTag {};
struct PlainTag {};
struct OutTag {};
struct FixtureBrand {};
using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using IoIn = c::PermissionedSpscChannel<IoPayload, 8, IoTag, FixtureBrand>;
using PlainIn = c::PermissionedSpscChannel<int, 8, PlainTag, FixtureBrand>;
using Out = c::PermissionedSpscChannel<int, 8, OutTag, FixtureBrand>;
inline void fan_in(IoIn::ConsumerHandle&&, PlainIn::ConsumerHandle&&, Out::ProducerHandle&&) noexcept {}

using Refused =
    decltype(c::mint_mpmc_stage<&fan_in>(std::declval<fixy::HotFgCtx const&>(), std::declval<IoIn::ConsumerHandle>(),
                                         std::declval<PlainIn::ConsumerHandle>(), std::declval<Out::ProducerHandle>()));
}  // namespace mpmc_row_fixture

int main() { return 0; }
