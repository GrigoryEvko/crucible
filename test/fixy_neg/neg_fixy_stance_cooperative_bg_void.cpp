// fixy_neg: stance::CooperativeBg<void> rejects via Type-axis gate.
//
// HS14 floor for FIXY-U-041 (CooperativeBg stance, void-Type angle).
// `void` is not an object type — the value-bearing wrapper cannot
// store one.  Same gate as BgWorker<void>; CooperativeBg differs
// from BgWorker in its Reentrancy axis (Coroutine vs strict
// NonReentrant) — but the Type-axis rejection is identical.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadCooperativeBgVoid = stance::CooperativeBg<void>;

static_assert(sizeof(BadCooperativeBgVoid) > 0,
    "instantiate stance::CooperativeBg<void> to force the Type-axis "
    "rejection (void is not an object type).");

int main() { return 0; }
