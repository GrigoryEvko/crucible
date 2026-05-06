// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-066 fixture #4: stripping a NumericalTier wrapper loses the
// static tolerance certificate.  A bare payload cannot flow into a
// BITEXACT-pinned boundary without an explicit tier-aware producer.

#include <crucible/sessions/SessionPayloadSubsort.h>

namespace proto = crucible::safety::proto;
namespace safe  = crucible::safety;

namespace {
struct Tile { float value[4]; };
using StrictTile = safe::NumericalTier<safe::Tolerance::BITEXACT, Tile>;
}  // namespace

static_assert(proto::is_subsort_v<Tile, StrictTile>,
    "NumericalTier_BarePayload_CannotGainTier");

int main() { return 0; }
