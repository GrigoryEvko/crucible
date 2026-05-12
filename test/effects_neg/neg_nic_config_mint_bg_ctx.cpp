// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-192. NIC configuration minting is Init-row work,
// not background drain work.

#include <crucible/cog/NicConfig.h>

namespace cog = crucible::cog;
namespace nic = crucible::cog::nic;
namespace cntp = crucible::cntp;
namespace eff = crucible::effects;

int main() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{1, 2};
    id.kind = cog::CogKind::NicPort;
    auto iface = cntp::NicInterfaceName::from("eth0").value();
    auto config = nic::mint_nic_config(eff::BgDrainCtx{}, id, iface);
    return config.has_value() ? 0 : 1;
}
