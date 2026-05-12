// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-192. Privileged NIC configuration apply requires
// a DeclaredNicConfig minted by the Init-row factory; raw plans must not
// cross the operator-policy boundary.

#include <crucible/cog/NicConfig.h>

namespace nic = crucible::cog::nic;

int main() {
    nic::NicConfigPlan raw{};
    auto result = nic::apply_config(raw);
    return result.has_value() ? 0 : 1;
}
