// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// NUMA placement of interrupts and queues is a property of a NIC port.
// The constraint of verify_numa_pinning refuses CogKind::Gpu before a
// report exists.

#include <crucible/cog/NumaNic.h>

#include <utility>

namespace cog = crucible::cog;

using Refused = decltype(cog::verify_numa_pinning<cog::CogKind::Gpu>(
    std::declval<cog::CogIdentity const&>(), std::declval<cog::caps_for_t<cog::CogKind::Gpu> const&>(),
    std::declval<cog::NumaNicFacts const&>()));

int main() { return 0; }
