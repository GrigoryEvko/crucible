// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 for GAPS-194. NUMA nodes are carried as NumaNodeId,
// not raw integers, so unknown/sentinel handling stays centralized and
// raw topology numbers cannot silently cross the verifier API.

#include <crucible/cog/NumaNic.h>

namespace cog = crucible::cog;

cog::NumaNicFacts facts{
    .nic_node = 0u,
};

int main() { return static_cast<int>(facts.nic_node.raw()); }
