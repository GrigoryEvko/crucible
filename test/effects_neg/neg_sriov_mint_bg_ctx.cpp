// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-147. SR-IOV plan minting is startup
// configuration work and requires an Init-row context.

#include <crucible/cog/SrIov.h>

namespace cog = crucible::cog;
namespace eff = crucible::effects;
namespace sriov = crucible::cog::sriov;

int main() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{1, 2};
    id.kind = cog::CogKind::NicPort;
    cog::NicPortTargetCaps caps{};
    caps.features.set(cog::NicFeature::SrIov);
    auto iface = crucible::cntp::NicInterfaceName::from("eth0").value();
    auto result = sriov::mint_sriov_plan(
        eff::BgDrainCtx{}, id, caps, iface, *sriov::admit_vf_count(1));
    return result.has_value() ? 0 : 1;
}
