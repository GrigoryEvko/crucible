// fixy_neg: stance::AsyncEndpoint<int[7]> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (AsyncEndpoint stance, distinct angle).
// An array Type would decay to a pointer in the wrapper's `Fn(Type v)`
// ctor, silently rebinding the value-semantic contract.  Same Type-
// axis gate; distinct rejection class from the reference fixture
// (decay-to-pointer vs not-an-object).
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadAsyncArray = stance::AsyncEndpoint<int[7]>;

static_assert(sizeof(BadAsyncArray) > 0,
    "instantiate stance::AsyncEndpoint<int[7]> to force the Type-"
    "axis rejection (array decay).");

int main() { return 0; }
