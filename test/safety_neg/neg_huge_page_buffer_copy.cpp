// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for safety::HugePageBuffer<T>
// (#944 WRAP-MetaLog-1 foundation).
//
// Premise: HugePageBuffer<T> is move-only RAII (carries the
// register_hot_region / madvise(MADV_HUGEPAGE) lifetime).  Copying
// would mean two destructors freeing the same alloc → double-free.
// The deleted copy ctor with reason makes the regression a compile
// error, not a runtime corruption.
//
// Distinct mismatch class from companion fixture
// neg_huge_page_buffer_use_after_move.cpp:
//   * This fixture: COPY-side gate (deleted copy ctor with reason).
//   * Companion:    MOVE-side gate (use-after-move under -Werror).

#include <crucible/safety/HugePageBuffer.h>

int main() {
    auto buf = crucible::safety::HugePageBuffer<int>::allocate(1024);
    auto copy{buf};   // deleted-copy-ctor compile error
    (void)copy;
    return 0;
}
