// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A probe outcome changes the quarantine state only from a context that
// owns the Bg atom.  The cold init context builds the policy, but it owns
// Init, Alloc and IO, and not Bg.

#include <crucible/warden/Quarantine.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::ColdInitCtx const startup{::foundation::effects::testing::init()};
    auto policy = crucible::warden::mint_quarantine_policy<2>(startup);
    crucible::cog::CogIdentity cog{};
    cog.uuid = crucible::cog::Uuid{0x118, 0x1};
    crucible::observe::ProbeOutcome outcome{};
    (void)policy.record_recovery_probe(startup, cog, outcome, 1);
    return 0;
}
