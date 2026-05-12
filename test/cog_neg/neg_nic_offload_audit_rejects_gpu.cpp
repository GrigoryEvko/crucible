// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 for GAPS-140. The audit is a NIC-port policy gate:
// a GPU CogKind has no TSO/GSO/GRO/RSS configuration surface, so the
// NicOffloadAuditableCog<K> requires clause must reject it before any
// runtime report can be fabricated.

#include <crucible/cog/NicOffloadAudit.h>

namespace cog = crucible::cog;

template <cog::CogKind K>
    requires cog::NicOffloadAuditableCog<K>
constexpr int audit_gate() noexcept { return 1; }

static_assert(audit_gate<cog::CogKind::Gpu>() == 1,
    "GAPS-140: NicOffloadAuditableCog must reject non-NicPort Cogs.");

int main() { return 0; }

