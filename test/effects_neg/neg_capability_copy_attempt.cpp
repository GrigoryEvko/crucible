// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-3 (#854): Capability linearity — copy is deleted.
//
// Violation: Capability<E, S> is move-only by linearity discipline.
// Attempting to copy-construct fires the deleted copy ctor.
//
// Expected diagnostic: "use of deleted function" / "copy
// constructor of 'Capability' is implicitly deleted" / similar.

#include <crucible/effects/Capability.h>

namespace eff = crucible::effects;

int main() {
    eff::Bg bg;
    auto cap = eff::mint_cap<eff::Effect::Alloc>(bg);
    auto bad = cap;  // copy: deleted
    (void)bad;
    return 0;
}
