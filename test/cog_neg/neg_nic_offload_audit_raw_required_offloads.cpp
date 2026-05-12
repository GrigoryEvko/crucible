// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 for GAPS-140. Required offloads are typed as
// safety::Bits<NicFeature>; a raw integer mask must not enter the
// policy surface, or TSO/GSO/GRO/RSS checks could silently consume a
// mask from the wrong feature universe.

#include <crucible/cog/NicOffloadAudit.h>

namespace cog = crucible::cog;

cog::NicOffloadAuditPolicy policy{
    .required_offloads = 1u,
};

int main() { return static_cast<int>(policy.required_offloads.raw()); }

