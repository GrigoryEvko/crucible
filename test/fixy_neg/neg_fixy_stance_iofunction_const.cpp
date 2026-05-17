// fixy_neg: stance::IoFunction<const int> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (IoFunction stance).  IoFunction pins
// Effect=IO but otherwise all-strict.  A top-level const Type fails
// IsAccepted: a const value cannot be assigned-into or moved-into
// the wrapper's field, breaking the wrapper's contract.  Use the
// underlying type and let the wrapper's grade discipline carry
// immutability if needed.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadIoConst = stance::IoFunction<const int>;

static_assert(sizeof(BadIoConst) > 0,
    "instantiate stance::IoFunction<const int> to force the Type-"
    "axis rejection (top-level const is not assignable).");

int main() { return 0; }
