// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// CogMimic was an aggregate, so a caller could bind an identity to caps of
// its own choice with no context, and the context gate of mint_cog_mimic
// held nothing shut.  The class has constructors now, so the aggregate form
// is refused: no constructor takes a pointer to the identity, and the one
// that takes a reference is private.

#include <crucible/mimic/CogMimic.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

namespace cog = crucible::cog;
namespace mimic = crucible::mimic;

int main() {
    cog::CogIdentity identity{};
    identity.uuid = cog::Uuid{0x1ULL, 0x2ULL};
    const mimic::CogMimic<cog::CogKind::Gpu> forged{
        &identity, ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(cog::GpuTargetCaps{}),
        cog::OpcodeLatencyTable<cog::CogKind::Gpu>{}};
    (void)forged;
    return 0;
}
