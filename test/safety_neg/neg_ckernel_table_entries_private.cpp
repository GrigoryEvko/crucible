// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The background thread classifies through a sealed kernel table with no
// lock.  A write to the entry array that does not go through register_op has
// no view to prove the table open and no seal phase to refuse it.  The entry
// array is private, so no such write compiles.

#include <crucible/CKernel.h>

int main() {
    crucible::CKernelTable table;
    table.seal();
    table.entries_[0].kernel_id = crucible::CKernelId::GEMM_MM;
    return 0;
}
