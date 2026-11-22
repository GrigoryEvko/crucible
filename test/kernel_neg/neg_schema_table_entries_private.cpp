// Negative-compile test. This file must fail to compile.
//
// Readers on other threads read a sealed schema table with no lock.  A write
// to the entry array that does not go through register_name has no view to
// prove the table open and no seal phase to refuse it.  The entry array is
// private, so no such write compiles.

#include <crucible/SchemaTable.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    crucible::SchemaTable table;
    table.seal(test);
    table.entries_.clear();
    return 0;
}
