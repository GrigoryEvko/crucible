// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An allocation has the lifetime of its arena.  A temporary arena releases
// its memory at the end of the statement, and the deleted `&&` twin of
// alloc_obj refuses a call on a temporary arena.
//
// Distinct mismatch class from neg_publish_static_ckernel_table_temporary.cpp:
//   * This fixture: the twin of a member refuses a temporary object.
//   * Companion: the twin of a free function refuses a temporary argument.

#include <crucible/Arena.h>

int main() {
    // MUST fail: the arena is a temporary.
    return crucible::Arena{}.alloc_obj<int>(::foundation::effects::testing::test().alloc) == nullptr ? 0 : 1;
}
