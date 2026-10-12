// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A single-writer body publishes into a writer.  The second parameter of
// this body is the producer handle of a channel, which has no publish, so
// the body is not a publish stage, and mint_swmr_stage refuses the call.
//
// Expected diagnostic: SwmrPublishStageBody is not satisfied.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Stage.h>

#include <utility>

namespace swmr_body_fixture {
namespace c = ::fixy::concurrent;

// The fixture builds no channel, so one named brand stands in for the
// brand of a root site.  The tags keep the two channels apart.
struct InTag {};
struct OutTag {};
struct FixtureBrand {};
using In = c::PermissionedSpscChannel<int, 8, InTag, FixtureBrand>;
using Out = c::PermissionedSpscChannel<int, 8, OutTag, FixtureBrand>;
inline void body(In::ConsumerHandle&&, Out::ProducerHandle&&) noexcept {}

using Refused = decltype(c::mint_swmr_stage<&body>(
    std::declval<fixy::HotFgCtx const&>(), std::declval<In::ConsumerHandle>(), std::declval<Out::ProducerHandle>()));
}  // namespace swmr_body_fixture

int main() { return 0; }
