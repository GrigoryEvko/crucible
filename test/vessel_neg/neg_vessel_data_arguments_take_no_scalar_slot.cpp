// Negative-compile test.  This file must fail to compile.
//
// Violation: a scalar slot of an op as the position of an argument of a
// schema.
//
// The set of the data slots of an op and the set of the data arguments of a
// schema are two domains.  A slot of the five inline slots is an Index<5>,
// and an Index<5> converts to the Index<32> of a position.  So each set keys
// its bits by the Side of its own role, and the slot does not reach the set
// of the arguments.

#include "../../vessel/torch/entry_slots.h"

#include <fixy/Core.h>

int main() {
    const crucible::ScalarSlot slot{::fixy::index<1>()};
    const crucible::vessel::DataArguments arguments = crucible::vessel::DataArguments{}.with_data(slot);
    return static_cast<int>(arguments.bits());
}
