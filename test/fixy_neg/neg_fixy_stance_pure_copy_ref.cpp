// fixy_neg: stance::PureCopy<int&> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (PureCopy stance).  PureCopy pins
// Usage=Copy but otherwise all-strict.  A reference Type fails
// IsAccepted's type-axis check (references are not objects and
// cannot satisfy NSDMI in the wrapper's field).
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadPureCopyRef = stance::PureCopy<int&>;

static_assert(sizeof(BadPureCopyRef) > 0,
    "instantiate stance::PureCopy<int&> to force its class-body "
    "static_assert (references are not object types).");

int main() { return 0; }
