// fixy_neg: stance::NamedSession<void, Proto> rejects via Type-axis gate.
//
// HS14 floor for FIXY-U-041 (NamedSession stance, void-Type angle).
// `void` is not an object type; `type_is_accepted_payload<void>`
// returns false, so IsAccepted rejects.  Distinct rejection class
// from the array fixture.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

struct DummyProto {};

using BadNamedSessionVoid = stance::NamedSession<void, DummyProto>;

static_assert(sizeof(BadNamedSessionVoid) > 0,
    "instantiate stance::NamedSession<void, Proto> to force the Type-"
    "axis rejection (void is not an object type).");

int main() { return 0; }
