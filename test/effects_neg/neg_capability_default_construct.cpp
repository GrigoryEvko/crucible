// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT-3 (#854): Capability default-ctor is private — cannot
// be reached except via mint_cap<>().
//
// Violation: forging a Capability<E, S> by direct default-construction
// would bypass the CanMintCap authorization gate.  The default ctor
// is private; only mint_cap<>() is a friend.  External direct
// construction fails.
//
// Expected diagnostic: "is private within this context" / "private
// member" pointing at the default ctor of Capability.

#include <crucible/effects/Capability.h>

namespace eff = crucible::effects;

int main() {
    eff::Capability<eff::Effect::Alloc, eff::Bg> bad{};  // default ctor private
    (void)bad;
    return 0;
}
