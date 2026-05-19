// fixy_neg: stance::CooperativeBg<int&> rejects via Type-axis gate.
//
// HS14 floor for FIXY-U-041 (CooperativeBg stance, reference angle).
// A reference Type would bind to an external lvalue at construction,
// defeating the wrapper's value-semantic ownership contract.  Distinct
// rejection class from the void fixture.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadCooperativeBgRef = stance::CooperativeBg<int&>;

static_assert(sizeof(BadCooperativeBgRef) > 0,
    "instantiate stance::CooperativeBg<int&> to force the Type-axis "
    "rejection (reference is not value-semantic).");

int main() { return 0; }
