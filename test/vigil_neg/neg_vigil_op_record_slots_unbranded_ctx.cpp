// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The record slots of a Vigil belong to the thread that holds its producer
// claim, and each thunk decodes the records of an op into them.  So
// op_record_slots() asks for the context of this Vigil's producer claim, and
// a context that names no claim is refused.

#include <crucible/Vigil.h>

int main() {
    crucible::Vigil vigil;
    crucible::OpRecordSlots& slots = vigil.op_record_slots(::foundation::effects::testing::foreground());
    (void)slots;
    return 0;
}
