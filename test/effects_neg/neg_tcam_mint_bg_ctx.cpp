#include <crucible/cntp/Tcam.h>

int main() {
    namespace cog = crucible::cog;
    namespace eff = crucible::effects;
    namespace saf = crucible::safety;
    namespace tcam = crucible::cntp::tcam;

    cog::CogIdentity nic{};
    nic.uuid = cog::Uuid{1, 2};
    nic.kind = cog::CogKind::NicPort;

    cog::NicPortTargetCaps caps{};
    caps.features.set(cog::NicFeature::Tcam);
    caps.tcam_entries = saf::Tagged<std::uint32_t, saf::source::Vendor>{4};

    auto table = tcam::mint_tcam_table(
        eff::BgDrainCtx{}, nic, caps, *tcam::admit_tcam_entries(1));
    (void)table;
    return 0;
}
