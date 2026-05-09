// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for safety::SwissTableBuffer<SlotPtr>
// (#915 WRAP-ExprPool-1 foundation).
//
// Premise: SwissTableBuffer is move-only RAII over a coupled (ctrl,
// slots) backing region.  Copying would make ctrl_/slots_ projection
// pointers alias between two instances; the second dtor would
// double-free.  Deleted copy ctor + reason makes the regression a
// compile error, not a runtime double-free.
//
// Distinct mismatch class from companion fixture
// neg_swiss_table_buffer_use_after_move.cpp:
//   * This fixture: COPY-side gate (deleted copy ctor with reason).
//   * Companion:    MOVE-side gate (use-after-move under -Werror).

#include <crucible/safety/SwissTableBuffer.h>

int main() {
    auto buf = crucible::safety::SwissTableBuffer<void*>::allocate(64);
    auto copy{buf};
    (void)copy;
    return 0;
}
