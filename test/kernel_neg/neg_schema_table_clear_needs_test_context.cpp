// Negative-compile test. This file must fail to compile.
//
// clear() empties the schema table and opens it again after the seal.  In
// production code that reopens a table that lock-free readers still read.
// clear() takes the test context, which only the test harness mints, so a
// call without that context does not compile.

#include <crucible/SchemaTable.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    crucible::SchemaTable table;
    table.seal(test);
    table.clear();
    return 0;
}
