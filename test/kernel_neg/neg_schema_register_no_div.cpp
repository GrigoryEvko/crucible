// Negative-compile test. This file must fail to compile.
//
// The claim of the write section takes the context of its writer: the
// context of a Vigil's producer claim, which takes the write section with one
// compare-exchange, or a context that owns Div, which spins while another
// writer holds the section.  The cold init context is neither, so it fails
// the gate, and no registration of it gets a section.
//
// Expected diagnostic: CtxMayEnterWriteSection is not satisfied for the cold
// init context.

#include <crucible/SchemaTable.h>
#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

int main() {
    const ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    crucible::SchemaTable table;
    return table.try_enter_write_section(init).is_some() ? 0 : 1;
}
