// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for safety::AlignedBuffer<T>
// (#876 WRAP-BgThread-5 foundation — also #944, #970).
//
// Premise: AlignedBuffer is move-only; copy-assignment is also
// deleted with reason.  This complements the deleted-copy-ctor
// fixture by exercising the assignment operator side of the
// move-only-RAII gate.  Together the two fixtures cover both
// initialization-time and assignment-time copies.
//
// Distinct mismatch class from companion fixture
// neg_aligned_buffer_copy.cpp:
//   * Companion:    COPY-CTOR-side gate (initialization).
//   * This fixture: COPY-ASSIGN-side gate (assignment).

#include <crucible/safety/AlignedBuffer.h>

int main() {
    auto a = crucible::safety::AlignedBuffer<int>::allocate(64);
    auto b = crucible::safety::AlignedBuffer<int>::allocate(64);
    // Copy-assignment is deleted with reason ("AlignedBuffer is
    // move-only") — this expression must fail to compile.
    a = b;
    return 0;
}
