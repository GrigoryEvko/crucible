// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A registration takes the context of its writer: the context of a Vigil's
// producer claim, which takes the write section with one compare-exchange,
// or a context that owns Div, which spins while another writer holds the
// section.  The cold init context is neither, so it fails the gate.
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
    const auto view = table.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>())
                          .expect("an open table mints a mutable view");
    (void)table.register_name(init, ::foundation::effects::testing::test().alloc, view, crucible::SchemaHash{0x42},
                              ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(::fixy::TextView{"aten::mm"}));
    return 0;
}
