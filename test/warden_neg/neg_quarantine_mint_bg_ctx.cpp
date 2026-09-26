// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_quarantine_policy refuses a background context.  A policy holds
// the quarantine state of a process, so only a context that owns the
// Init atom builds one.  The background drain context owns Bg and Alloc.

#include <crucible/warden/Quarantine.h>
#include <fixy/Ctx.h>

int main() {
    auto policy = crucible::warden::mint_quarantine_policy<2>(::fixy::BgDrainCtx{::foundation::effects::testing::bg()});
    (void)policy;
    return 0;
}
