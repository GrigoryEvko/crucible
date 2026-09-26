// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A policy names its required offloads as a fixy::Bits<NicFeature>. A
// raw integer mask does not convert, so a mask from a different feature
// enum cannot reach the offload checks.

#include <crucible/cog/NicOffloadAudit.h>

namespace cog = crucible::cog;

cog::NicOffloadAuditPolicy policy{
    .required_offloads = 1u,
};

int main() { return static_cast<int>(policy.required_offloads.raw()); }
