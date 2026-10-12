// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The fan-in body takes two consumer handles of channel In and one producer
// handle of channel Out.  The second handle is a consumer handle of channel
// Other, so the handles do not name the parameters of the body, and
// mint_mpmc_stage refuses the call.
//
// Expected diagnostic: VariadicStageHandlesMatch is not satisfied.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Stage.h>

#include <utility>

namespace mpmc_mismatch_fixture {
namespace c = ::fixy::concurrent;

// The fixture builds no channel, so one named brand stands in for the
// brand of a root site.  The tags keep the three channels apart.
struct InTag {};
struct OtherTag {};
struct OutTag {};
struct FixtureBrand {};
using In = c::PermissionedSpscChannel<int, 8, InTag, FixtureBrand>;
using Other = c::PermissionedSpscChannel<int, 8, OtherTag, FixtureBrand>;
using Out = c::PermissionedSpscChannel<int, 8, OutTag, FixtureBrand>;
inline void fan_in(In::ConsumerHandle&&, In::ConsumerHandle&&, Out::ProducerHandle&&) noexcept {}

using Refused =
    decltype(c::mint_mpmc_stage<&fan_in>(std::declval<fixy::HotFgCtx const&>(), std::declval<In::ConsumerHandle>(),
                                         std::declval<Other::ConsumerHandle>(), std::declval<Out::ProducerHandle>()));
}  // namespace mpmc_mismatch_fixture

int main() { return 0; }
