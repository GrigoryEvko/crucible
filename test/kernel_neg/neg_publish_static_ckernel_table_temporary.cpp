// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The readers of the process keep the address of the published table.  The
// lifetime of a temporary table stops at the end of the statement, and the
// deleted twin of publish_static_ckernel_table refuses a temporary table.
//
// Distinct mismatch class from neg_arena_alloc_on_temporary.cpp:
//   * This fixture: the twin of a free function refuses a temporary argument.
//   * Companion: the twin of a member refuses a temporary object.

#include <crucible/CKernel.h>

int main() {
    // MUST fail: the table is a temporary.
    crucible::publish_static_ckernel_table(crucible::StaticCKernelTable{});
    return 0;
}
