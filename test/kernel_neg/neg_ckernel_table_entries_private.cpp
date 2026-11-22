// Negative-compile test. This file must fail to compile.
//
// The background thread classifies through a sealed kernel table with no
// lock.  A write to the entry array that does not go through register_op has
// no view to prove the table open and no seal phase to refuse it.  The entry
// array is private, so no such write compiles.

#include <crucible/CKernel.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    crucible::CKernelTable table;
    table.seal(test);
    table.entries_.clear();
    return 0;
}
