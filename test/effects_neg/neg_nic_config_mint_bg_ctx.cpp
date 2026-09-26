// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A NIC configuration is startup work. The mint admits a context whose
// row holds Init, and the background drain row does not.

#include <crucible/cog/NicConfig.h>

namespace cog = crucible::cog;
namespace nic = crucible::cog::nic;
namespace cntp = crucible::cntp;

int main() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{1, 2};
    id.kind = cog::CogKind::NicPort;
    auto iface = cntp::NicInterfaceName::from("eth0").value();
    auto config = nic::mint_nic_config(::fixy::BgDrainCtx{::foundation::effects::testing::bg()}, id, iface);
    return config.has_value() ? 0 : 1;
}
