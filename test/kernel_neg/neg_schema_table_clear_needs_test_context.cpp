// Negative-compile test. This file must fail to compile.
//
// clear() empties the schema table and opens it again after the seal.  In
// production code that reopens a table that lock-free readers still read.
// clear() takes the proof of a reopen, which only the test context builds,
// and only the test harness mints that context.  So a call without that
// proof does not compile.

#include <crucible/SchemaTable.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    crucible::SchemaTable table;
    table.seal(test);
    table.clear();
    return 0;
}
