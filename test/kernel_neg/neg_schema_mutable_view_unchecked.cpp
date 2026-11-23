// Negative-compile test. This file must fail to compile.
//
// A sealed schema table mints no mutable view, so mint_mutable_view
// returns an optional view.  A registration that skips the check and
// hands the optional itself to register_name does not compile, so no
// caller writes to a sealed table by forgetting the seal.

#include <crucible/SchemaTable.h>

int main() {
    crucible::SchemaTable table;
    table.register_name(::foundation::effects::testing::foreground<crucible::Vigil>(),
                        ::foundation::effects::testing::test().alloc,
                        table.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>()),
                        crucible::SchemaHash{0x42}, crucible::InternalSchemaName{::fixy::TextView{"aten::mm"}});
    return 0;
}
