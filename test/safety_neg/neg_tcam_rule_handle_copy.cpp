// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An installed rule has one owner.  The handle that add_rule returns is
// Linear, so a copy of it is refused.

#include <crucible/cntp/Tcam.h>
#include <fixy/Ctx.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>

int main() {
    namespace cog = crucible::cog;
    namespace tcam = crucible::cntp::tcam;

    cog::CogIdentity nic{};
    nic.uuid = cog::Uuid{1, 2};
    nic.kind = cog::CogKind::NicPort;
    cog::NicPortTargetCaps caps{};
    caps.features.set(cog::NicFeature::Tcam);
    caps.tcam_entries = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint32_t>(4);

    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto plan = tcam::mint_tcam_table(init, nic, caps, tcam::admit_tcam_entries(4).value());
    tcam::TcamRules<4> table{plan.value()};
    auto handle = table.add_rule(tcam::declare_tcam_rule(tcam::TcamFlowRule{}).value());
    auto copy = handle.value();
    (void)copy;
    return 0;
}
