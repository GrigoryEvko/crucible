// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-066 fixture #6: a loop body cannot silently alternate from
// BITEXACT to RELAXED where the protocol contract requires BITEXACT
// at every iteration step.  The failure must be visible through
// recursive SessionSubtype composition, not only at a flat Send.

#include <crucible/sessions/SessionPayloadSubsort.h>

namespace proto = crucible::safety::proto;
namespace safe  = crucible::safety;

namespace {
struct Tile { float value[4]; };

using RelaxedTile = safe::NumericalTier<safe::Tolerance::RELAXED, Tile>;
using StrictTile  = safe::NumericalTier<safe::Tolerance::BITEXACT, Tile>;

using DowngradingLoop = proto::Loop<
    proto::Send<StrictTile,
    proto::Send<RelaxedTile, proto::Continue>>>;

using StrictLoop = proto::Loop<
    proto::Send<StrictTile,
    proto::Send<StrictTile, proto::Continue>>>;
}  // namespace

static_assert(proto::is_subtype_sync_v<DowngradingLoop, StrictLoop>,
    "NumericalTier_LoopBody_TierDowngrade");

int main() { return 0; }
