// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The audit reads the facts of a NIC port. A GPU has no offload, queue
// or RSS setting, so the constraint of audit_nic_offloads refuses
// CogKind::Gpu before a report exists.

#include <crucible/cog/NicOffloadAudit.h>

#include <utility>

namespace cog = crucible::cog;

using Refused = decltype(cog::audit_nic_offloads<cog::CogKind::Gpu>(
    std::declval<cog::CogIdentity const&>(), std::declval<cog::caps_for_t<cog::CogKind::Gpu> const&>(),
    std::declval<cog::NicOffloadAuditFacts const&>()));

int main() { return 0; }
