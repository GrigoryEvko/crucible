// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The constructor of CogMimic that binds an identity is private, and
// mint_cog_mimic is its one friend.  So a caller that holds an identity,
// caps under the Calibrated tag and an opcode table still cannot bind them
// without a context that owns Init or Bg.

#include <crucible/mimic/CogMimic.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

namespace cog = crucible::cog;
namespace mimic = crucible::mimic;

int main() {
    cog::CogIdentity identity{};
    identity.uuid = cog::Uuid{0x1ULL, 0x2ULL};
    const mimic::CogMimic<cog::CogKind::Gpu> forged{
        identity, ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(cog::GpuTargetCaps{}),
        cog::OpcodeLatencyTable<cog::CogKind::Gpu>{}};
    (void)forged;
    return 0;
}
