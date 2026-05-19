// fixy_neg: stance::SyncBlocking<void> rejects via Type-axis gate.
//
// HS14 floor for FIXY-U-041 (SyncBlocking stance, void-Type angle).
// `void` is not an object type — the value-bearing wrapper cannot
// store one.  Distinct rejection class from the array fixture.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadSyncBlockingVoid = stance::SyncBlocking<void>;

static_assert(sizeof(BadSyncBlockingVoid) > 0,
    "instantiate stance::SyncBlocking<void> to force the Type-axis "
    "rejection (void is not an object type).");

int main() { return 0; }
