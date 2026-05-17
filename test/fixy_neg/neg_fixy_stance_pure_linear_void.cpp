// fixy_neg: stance::PureLinear<void> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (PureLinear stance, distinct angle).
// `void` is not an object type; `type_is_object_or_function<void>`
// returns false, so IsAccepted rejects.  Distinct failure mode from
// the array-Type fixture: array decay vs the void-is-not-object
// rejection class.

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

// `void` is not an object type — would also break value_type / NSDMI
// in the class body.  Same IsAccepted gate, different rejection
// pathway.  Force class-body instantiation via sizeof on the type
// directly (NOT on a pointer to it — that wouldn't complete the class).
using BadPureLinearVoid = stance::PureLinear<void>;

static_assert(sizeof(BadPureLinearVoid) > 0,
    "instantiate stance::PureLinear<void> to force its class-body "
    "static_assert (void is not an object type).");

int main() { return 0; }
