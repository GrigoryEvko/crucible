// fixy_neg: stance::NamedSession<int[7], Proto> rejects via Type-axis gate.
//
// HS14 floor for FIXY-U-041 (NamedSession stance, array-Type angle).
// An array Type would decay to a pointer in the wrapper's value
// ctor, silently rebinding the value-semantic contract.  Distinct
// rejection class from the void fixture (decay-to-pointer vs
// not-an-object).
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

struct DummyProto {};

using BadNamedSessionArray = stance::NamedSession<int[7], DummyProto>;

static_assert(sizeof(BadNamedSessionArray) > 0,
    "instantiate stance::NamedSession<int[7], Proto> to force the "
    "Type-axis rejection (array decay).");

int main() { return 0; }
