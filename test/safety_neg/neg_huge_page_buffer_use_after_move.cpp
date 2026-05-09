// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for safety::HugePageBuffer<T>
// (#944 WRAP-MetaLog-1 foundation).
//
// Premise: HugePageBuffer is move-only.  Copy-assignment is deleted
// with reason ("HugePageBuffer is move-only").  This complements the
// deleted-copy-ctor fixture by exercising the assignment-operator
// side of the move-only-RAII gate.

#include <crucible/safety/HugePageBuffer.h>

int main() {
    auto a = crucible::safety::HugePageBuffer<int>::allocate(1024);
    auto b = crucible::safety::HugePageBuffer<int>::allocate(1024);
    a = b;   // deleted copy-assignment → compile error
    return 0;
}
