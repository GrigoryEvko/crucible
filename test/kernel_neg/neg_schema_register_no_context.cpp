// Negative-compile test. This file must fail to compile.
//
// A registration writes the table inside its write section.  The claim door
// takes the context of the writer and gives the section, and each writer door
// takes the section.  So a registration with no section and no context does
// not compile.
//
// Expected diagnostic: SchemaTable::register_name has no candidate that takes
// no section.

#include <crucible/SchemaTable.h>
#include <crucible/Vigil.h>

int main() {
    crucible::SchemaTable table;
    const auto view = table.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>())
                          .expect("an open table mints a mutable view");
    (void)table.register_name(::foundation::effects::testing::test().alloc, view,
                              crucible::InternalSchemaName{crucible::SchemaHash{0x42}, ::fixy::TextView{"aten::mm"}});
    return 0;
}
