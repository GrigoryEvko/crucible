// Negative-compile test. This file must fail to compile.
//
// A sealed schema table mints no mutable view, so mint_mutable_view
// returns an optional view.  A registration that skips the check and
// hands the optional itself to register_name does not compile, so no
// caller writes to a sealed table by forgetting the seal.

#include <crucible/SchemaTable.h>

// A write section of the table.  The fixture never links, so the function
// has no body.
crucible::WriteSection<crucible::SchemaTable>& held_section() noexcept;

int main() {
    crucible::SchemaTable table;
    table.register_name(held_section(), ::foundation::effects::testing::test().alloc,
                        table.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>()),
                        crucible::InternalSchemaName{crucible::SchemaHash{0x42}, ::fixy::TextView{"aten::mm"}});
    return 0;
}
