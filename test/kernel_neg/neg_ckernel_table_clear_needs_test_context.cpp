// Negative-compile test. This file must fail to compile.
//
// clear() empties the kernel table and opens it again after the seal.  In
// production code that reopens a table that the background thread still
// reads with no lock.  clear() takes the test context, which only the test
// harness mints, so a call without that context does not compile.

#include <crucible/CKernel.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    crucible::CKernelTable table;
    table.seal(test);
    table.clear();
    return 0;
}
