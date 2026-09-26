// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// add_rule takes a rule that declare_tcam_rule checked.  A raw rule does
// not convert to the declared rule, so add_rule refuses it.  The table
// comes from its mint, so the rule is the one thing refused.

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
    tcam::TcamFlowRule raw{};
    auto added = table.add_rule(raw);
    (void)added;
    return 0;
}
