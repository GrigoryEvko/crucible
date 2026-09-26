// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_quarantine_policy refuses the hot foreground context.  Its row is
// empty, so it does not own the Init atom that the gate asks for.

#include <crucible/warden/Quarantine.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::HotFgCtx const foreground = ::foundation::effects::testing::foreground();
    auto policy = crucible::warden::mint_quarantine_policy<2>(foreground);
    (void)policy;
    return 0;
}
