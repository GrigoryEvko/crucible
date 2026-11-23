// Negative-compile test. This file must fail to compile.
//
// register_name takes a SchemaTable::MutableView.  A SealedView does not
// convert to a MutableView, and no overload takes a SealedView.  When the
// table is sealed, no call reaches register_name.

#include <crucible/SchemaTable.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    crucible::SchemaTable t;
    t.seal(test);
    auto sv = t.mint_sealed_view();

    t.register_name(test, ::foundation::effects::testing::test().alloc, sv, crucible::SchemaHash{0x42},
                    crucible::InternalSchemaName{::fixy::TextView{"aten::mm"}});
    return 0;
}
