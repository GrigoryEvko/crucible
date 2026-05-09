// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for safety::AlignedBuffer<T>
// (#876 WRAP-BgThread-5 foundation — also #944, #970).
//
// Premise: AlignedBuffer<T> is move-only RAII.  The deleted copy ctor
// carries a reason string ("AlignedBuffer is move-only"); attempting
// to copy must be rejected at compile time so the type system carries
// the consume-once obligation through every BgThread scratch buffer.
//
// Distinct mismatch class from companion fixture
// neg_aligned_buffer_use_after_move.cpp:
//   * This fixture: COPY-side gate (deleted copy ctor with reason).
//   * Companion:    MOVE-side gate (use-after-move via -Werror=use-after-move).

#include <crucible/safety/AlignedBuffer.h>

int main() {
    auto buf = crucible::safety::AlignedBuffer<int>::allocate(64);
    // Copy ctor is deleted with reason — compile error.
    auto copy{buf};
    (void)copy;
    return 0;
}
