// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The seal of a table waits with no exit while a writer holds the write
// section, so it asks for a context that owns Div.  The cold init context
// owns Init, Alloc and IO, and no Div, so it fails the gate.
//
// Expected diagnostic: CtxOwnsCapability of Div is not satisfied for the
// cold init context.

#include <crucible/SchemaTable.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

int main() {
    const ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    crucible::SchemaTable table;
    table.seal(init);
    return 0;
}
