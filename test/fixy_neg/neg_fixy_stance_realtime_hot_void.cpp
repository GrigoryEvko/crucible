// fixy_neg: stance::RealtimeHot<void> rejects via Type-axis gate.
//
// HS14 floor for FIXY-U-041 (RealtimeHot stance, void-Type angle).
// `void` is not an object type — the value-bearing wrapper cannot
// store one.  Distinct rejection class from the reference fixture.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadRealtimeHotVoid = stance::RealtimeHot<void>;

static_assert(sizeof(BadRealtimeHotVoid) > 0,
    "instantiate stance::RealtimeHot<void> to force the Type-axis "
    "rejection (void is not an object type).");

int main() { return 0; }
