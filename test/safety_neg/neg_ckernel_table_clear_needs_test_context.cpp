// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// clear() empties the kernel table and opens it again after the seal.  In
// production code that reopens a table that the background thread still
// reads with no lock.  clear() takes the test context, which only the test
// harness mints, so a call without that context does not compile.

#include <crucible/CKernel.h>

int main() {
    crucible::CKernelTable table;
    table.seal();
    table.clear();
    return 0;
}
