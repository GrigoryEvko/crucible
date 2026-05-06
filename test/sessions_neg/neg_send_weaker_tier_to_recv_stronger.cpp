// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-066 fixture #1: a producer pinned at a weaker numerical tier
// must not be channel-compatible with a receiver requiring BITEXACT.
// CompatibleClient<Send<P>, Recv<C>> reduces to P <= C through
// SessionSubtype's Send covariance and the receiver's dual.

#include <crucible/sessions/SessionPayloadSubsort.h>

namespace proto = crucible::safety::proto;
namespace safe  = crucible::safety;

namespace {
struct Tile { float value[4]; };

using RelaxedTile = safe::NumericalTier<safe::Tolerance::RELAXED, Tile>;
using StrictTile  = safe::NumericalTier<safe::Tolerance::BITEXACT, Tile>;

using Producer = proto::Send<RelaxedTile, proto::End>;
using Consumer = proto::Recv<StrictTile, proto::End>;
}  // namespace

static_assert(proto::CompatibleClient<Producer, Consumer>,
    "NumericalTier_ChannelBoundary_ProducerTierTooWeak");

int main() { return 0; }
