// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// clear() empties the schema table and opens it again after the seal.  In
// production code that reopens a table that lock-free readers still read.
// clear() takes the test context, which only the test harness mints, so a
// call without that context does not compile.

#include <crucible/SchemaTable.h>

int main() {
    crucible::SchemaTable table;
    table.seal();
    table.clear();
    return 0;
}
