// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-066 fixture #2: receiving a RELAXED value does not authorize
// downstream code to treat that receive endpoint as BITEXACT.  Recv is
// contravariant, but it cannot conjure a stronger payload certificate.

#include <crucible/sessions/SessionPayloadSubsort.h>

namespace proto = crucible::safety::proto;
namespace safe  = crucible::safety;

namespace {
struct Tile { float value[4]; };

using RelaxedTile = safe::NumericalTier<safe::Tolerance::RELAXED, Tile>;
using StrictTile  = safe::NumericalTier<safe::Tolerance::BITEXACT, Tile>;

using ActualRecv     = proto::Recv<RelaxedTile, proto::End>;
using PromotedRecv   = proto::Recv<StrictTile, proto::End>;
}  // namespace

static_assert(proto::is_subtype_sync_v<PromotedRecv, ActualRecv>,
    "NumericalTier_RecvPromotion_RequiresTightening");

int main() { return 0; }
