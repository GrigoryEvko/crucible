// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for safety::SwissTableBuffer<SlotPtr>
// (#915 WRAP-ExprPool-1 foundation).
//
// Premise: SwissTableBuffer is move-only.  Copy-assignment is deleted
// with reason ("SwissTableBuffer is move-only").  This complements
// the deleted-copy-ctor fixture by exercising the assignment-operator
// side of the move-only-RAII gate.

#include <crucible/safety/SwissTableBuffer.h>

int main() {
    auto a = crucible::safety::SwissTableBuffer<void*>::allocate(64);
    auto b = crucible::safety::SwissTableBuffer<void*>::allocate(64);
    a = b;   // deleted copy-assignment → compile error
    return 0;
}
