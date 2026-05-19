// fixy_neg: stance::SyncBlocking<int[7]> rejects via Type-axis gate.
//
// HS14 floor for FIXY-U-041 (SyncBlocking stance, array-Type angle).
// An array Type would decay to a pointer in the wrapper's value
// ctor, silently rebinding the value-semantic contract.  Distinct
// rejection class from the void fixture.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadSyncBlockingArray = stance::SyncBlocking<int[7]>;

static_assert(sizeof(BadSyncBlockingArray) > 0,
    "instantiate stance::SyncBlocking<int[7]> to force the Type-axis "
    "rejection (array decay).");

int main() { return 0; }
