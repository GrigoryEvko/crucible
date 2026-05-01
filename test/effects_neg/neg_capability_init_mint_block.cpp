// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-3 (#854): Init context cannot mint Effect::Block.
//
// Violation: Init's permitted row is {Init, Alloc, IO} — explicitly
// excludes Block (Init must never block on a sync primitive).
// mint_cap<Effect::Block>(Init{}) fails CanMintCap.
//
// Expected diagnostic: GCC's "associated constraints are not
// satisfied" pointing at CanMintCap.

#include <crucible/effects/Capability.h>

namespace eff = crucible::effects;

int main() {
    eff::Init init;
    auto bad = eff::mint_cap<eff::Effect::Block>(init);  // Init doesn't permit Block
    (void)bad;
    return 0;
}
