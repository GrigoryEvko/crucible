// Negative-compile test.  This file must fail to compile.
//
// The record stack of a Vigil belongs to the thread that holds its producer
// claim, and each adapter writes the records of an op into it.  So
// op_record_stack() asks for the context of this Vigil's producer claim, and
// a context that names no claim is refused.

#include <crucible/Vigil.h>

int main() {
    crucible::Vigil vigil;
    crucible::OpRecordStack& stack = vigil.op_record_stack(::foundation::effects::testing::foreground());
    (void)stack;
    return 0;
}
