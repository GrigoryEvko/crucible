// Negative compile test.  This file must fail to compile.
//
// A single-writer body publishes into a writer.  The second parameter of
// this body is the producer end of a channel, which has no publish, so the
// body is not a publish stage, and mint_swmr_stage refuses the call.
//
// Expected diagnostic: SwmrPublishStageBody is not satisfied.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/concurrent/Stage.h>

#include <utility>

namespace swmr_body_fixture {
namespace c = ::fixy::concurrent;

// The fixture builds no channel, so one named brand stands in for the
// brand of a root site.  The tags keep the two channels apart.  The shape
// of the body is a fact of the types, so the body takes the ends of the
// channels, and no frame door opens a handle.
struct InTag {};
struct OutTag {};
struct FixtureBrand {};
using In = c::PermissionedChannel<c::ChannelForm::wire, int, 8, InTag, FixtureBrand>;
using Out = c::PermissionedChannel<c::ChannelForm::wire, int, 8, OutTag, FixtureBrand>;
inline void body(In::ConsumerEnd&&, Out::ProducerEnd&&) noexcept {}

using Refused = decltype(c::mint_swmr_stage<&body>(std::declval<fixy::HotFgCtx const&>(),
                                                   std::declval<In::ConsumerEnd>(), std::declval<Out::ProducerEnd>()));
}  // namespace swmr_body_fixture

int main() { return 0; }
