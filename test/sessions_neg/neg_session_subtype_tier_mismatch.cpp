// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-066 fixture #3: Send is covariant in payload, so a RELAXED
// producer cannot subtype a BITEXACT producer contract.

#include <crucible/sessions/SessionPayloadSubsort.h>

namespace proto = crucible::safety::proto;
namespace safe  = crucible::safety;

namespace {
struct Tile { float value[4]; };

using RelaxedTile = safe::NumericalTier<safe::Tolerance::RELAXED, Tile>;
using StrictTile  = safe::NumericalTier<safe::Tolerance::BITEXACT, Tile>;

using WeakProducer   = proto::Send<RelaxedTile, proto::End>;
using StrictProducer = proto::Send<StrictTile, proto::End>;
}  // namespace

static_assert(proto::is_subtype_sync_v<WeakProducer, StrictProducer>,
    "NumericalTier_SendSubtype_ProducerTierTooWeak");

int main() { return 0; }
