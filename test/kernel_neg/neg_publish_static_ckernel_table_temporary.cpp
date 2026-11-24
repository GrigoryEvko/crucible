// Negative-compile test. This file must fail to compile.
//
// The readers of the process keep the address of the published table.  The
// lifetime of a temporary table stops at the end of the statement.  The table
// is the template argument of a reference type of the door, and a temporary is
// no such argument.
//
// Distinct mismatch class from
// test/kernel_neg/neg_publish_static_ckernel_table_local.cpp:
//   * This fixture: the template argument is a temporary.
//   * Companion: the template argument is a local object.

#include <crucible/CKernel.h>

int main() {
    // must fail: the table is a temporary.
    crucible::publish_static_ckernel_table<crucible::StaticCKernelTable{}>();
    return 0;
}
