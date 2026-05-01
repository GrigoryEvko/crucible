// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-3 (#854): Capability mint-from-foreground rejection.
//
// Violation: foreground (ctx_cap::Fg) has cap_permitted_row == Row<>,
// so CanMintCap<Effect::Alloc, Fg> is false.  mint_cap<Alloc>(Fg{})
// fails the requires-clause at the call site.
//
// Expected diagnostic: GCC's "associated constraints are not
// satisfied" pointing at CanMintCap.

#include <crucible/effects/Capability.h>

namespace eff = crucible::effects;

int main() {
    eff::ctx_cap::Fg fg;
    auto bad = eff::mint_cap<eff::Effect::Alloc>(fg);  // CanMintCap fails
    (void)bad;
    return 0;
}
