// fixy_neg: stance::AsyncEndpoint<int&> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (AsyncEndpoint stance).  AsyncEndpoint
// pins Effect=IO + Reentrancy=Coroutine — the typical shape of an
// async endpoint that suspends across an IO trip.  A reference Type
// is not an object and fails IsAccepted.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadAsyncRef = stance::AsyncEndpoint<int&>;

static_assert(sizeof(BadAsyncRef) > 0,
    "instantiate stance::AsyncEndpoint<int&> to force the Type-axis "
    "rejection (references are not objects).");

int main() { return 0; }
