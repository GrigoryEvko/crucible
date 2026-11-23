// Negative compile test.  This file must fail to compile.
//
// The fan-in body takes two consumer ends of channel In and one producer
// end of channel Out.  The second end is a consumer end of channel Other,
// so the ends do not name the parameters of the body, and mint_mpmc_stage
// refuses the call.
//
// Expected diagnostic: VariadicStageHandlesMatch is not satisfied.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/concurrent/Stage.h>

#include <utility>

namespace mpmc_mismatch_fixture {
namespace c = ::fixy::concurrent;

// The fixture builds no channel, so one named brand stands in for the
// brand of a root site.  The tags keep the three channels apart.  The
// match of the handles is a fact of the types, so the body takes the ends
// of the channels, and no frame door opens a handle.
struct InTag {};
struct OtherTag {};
struct OutTag {};
struct FixtureBrand {};
using In = c::PermissionedChannel<c::ChannelForm::wire, int, 8, InTag, FixtureBrand>;
using Other = c::PermissionedChannel<c::ChannelForm::wire, int, 8, OtherTag, FixtureBrand>;
using Out = c::PermissionedChannel<c::ChannelForm::wire, int, 8, OutTag, FixtureBrand>;
inline void fan_in(In::ConsumerEnd&&, In::ConsumerEnd&&, Out::ProducerEnd&&) noexcept {}

using Refused =
    decltype(c::mint_mpmc_stage<&fan_in>(std::declval<fixy::HotFgCtx const&>(), std::declval<In::ConsumerEnd>(),
                                         std::declval<Other::ConsumerEnd>(), std::declval<Out::ProducerEnd>()));
}  // namespace mpmc_mismatch_fixture

int main() { return 0; }
