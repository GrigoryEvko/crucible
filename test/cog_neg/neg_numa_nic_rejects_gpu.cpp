// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 for GAPS-194. NIC/NUMA pinning is meaningful only for
// a NicPort CogKind; the NumaNicAuditableCog<K> requires clause must
// reject GPU Cogs before a runtime report can be fabricated.

#include <crucible/cog/NumaNic.h>

namespace cog = crucible::cog;

template <cog::CogKind K>
    requires cog::NumaNicAuditableCog<K>
constexpr int audit_gate() noexcept { return 1; }

static_assert(audit_gate<cog::CogKind::Gpu>() == 1,
    "GAPS-194: NumaNicAuditableCog must reject non-NicPort Cogs.");

int main() { return 0; }
