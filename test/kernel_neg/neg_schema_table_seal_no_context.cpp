// Negative-compile test. This file must fail to compile.
//
// The seal of a table waits while a writer holds the write section, with no
// exit.  A spin with no exit carries Div, so the seal takes the context of the
// caller, and the context owns Div.  A seal with no context would spin from
// each context, the hot path included.
//
// Expected diagnostic: SchemaTable::seal has no candidate that takes no
// argument.

#include <crucible/SchemaTable.h>

int main() {
    crucible::SchemaTable table;
    table.seal();
    return 0;
}
