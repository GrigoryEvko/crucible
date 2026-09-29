// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A table plan is start-up work, so its mint takes a context that admits
// the initialization row.  A background drain context does not, and the
// gate refuses it.  The target and its caps carry a TCAM, so the context
// is the one thing refused.

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

    ::fixy::BgDrainCtx drain{::foundation::effects::testing::bg()};
    auto table = tcam::mint_tcam_table(drain, nic, caps, tcam::admit_tcam_entries(1).value());
    (void)table;
    return 0;
}
