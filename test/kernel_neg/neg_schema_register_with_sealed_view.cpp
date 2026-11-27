// Negative-compile test. This file must fail to compile.
//
// register_name takes a SchemaTable::MutableView.  A SealedView does not
// convert to a MutableView, and no overload takes a SealedView.  When the
// table is sealed, no call reaches register_name.

#include <crucible/SchemaTable.h>
#include <fixy/Ctx.h>

// A write section of the table.  The fixture never links, so the function
// has no body.
crucible::WriteSection<crucible::SchemaTable>& held_section() noexcept;

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    crucible::SchemaTable t;
    t.seal(test);
    auto sv = t.mint_sealed_view();

    t.register_name(held_section(), ::foundation::effects::testing::test().alloc, sv,
                    crucible::InternalSchemaName{crucible::SchemaHash{0x42}, ::fixy::TextView{"aten::mm"}});
    return 0;
}
