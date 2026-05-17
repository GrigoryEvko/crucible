// fixy_neg: stance::BgWorker<void> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (BgWorker stance, distinct angle).
// `void` is not an object type and fails IsAccepted.  Distinct from
// the rvalue-ref fixture: void-as-incomplete vs ref-not-object.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadBgVoid = stance::BgWorker<void>;

static_assert(sizeof(BadBgVoid) > 0,
    "instantiate stance::BgWorker<void> to force the Type-axis "
    "rejection (void is not an object type).");

int main() { return 0; }
