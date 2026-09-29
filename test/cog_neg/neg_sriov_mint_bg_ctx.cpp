// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// An SR-IOV plan is startup work. The mint admits a context whose row
// holds Init, and the background drain row does not.

#include <crucible/cog/SrIov.h>

namespace cog = crucible::cog;
namespace sriov = crucible::cog::sriov;

int main() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{1, 2};
    id.kind = cog::CogKind::NicPort;
    cog::NicPortTargetCaps caps{};
    caps.features.set(cog::NicFeature::SrIov);
    auto iface = crucible::cntp::NicInterfaceName::from("eth0").value();
    auto result = sriov::mint_sriov_plan(::fixy::BgDrainCtx{::foundation::effects::testing::bg()}, id, caps, iface,
                                         *sriov::admit_vf_count(1));
    return result.has_value() ? 0 : 1;
}
