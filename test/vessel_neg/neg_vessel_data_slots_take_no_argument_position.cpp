// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: the position of an argument of a schema as a scalar slot of an
// op.
//
// The set of the data slots of an op keys its bits by the Side of the role
// of an inline slot, so the position of an argument, in the role of an
// argument, does not reach it, even when its number is below five.

#include "../../vessel/torch/entry_slots.h"

#include <fixy/Core.h>

int main() {
    const crucible::vessel::ArgumentPosition position{::fixy::index<1>()};
    const crucible::DataSlots slots = crucible::DataSlots{}.with(position, crucible::ScalarRole::DATA);
    return static_cast<int>(slots.bits());
}
