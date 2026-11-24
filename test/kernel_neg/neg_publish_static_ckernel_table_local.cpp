// Negative-compile test. This file must fail to compile.
//
// The readers of the process keep the address of the published table, and a
// local table ends with the frame of its function.  The door took the table
// by a reference before, so a local table compiled and its address dangled
// after the return.  The table is now the template argument of a reference
// type of the door, and a local object is no such argument.
//
// Distinct mismatch class from
// test/kernel_neg/neg_publish_static_ckernel_table_temporary.cpp:
//   * This fixture: the template argument is a local object.
//   * Companion: the template argument is a temporary.

#include <crucible/CKernel.h>

int main() {
    const crucible::StaticCKernelTable local{};
    // must fail: the table is a local object.
    crucible::publish_static_ckernel_table<local>();
    return 0;
}
