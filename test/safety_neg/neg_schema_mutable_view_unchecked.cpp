// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A sealed schema table mints no mutable view, so mint_mutable_view
// returns an optional view.  A registration that skips the check and
// hands the optional itself to register_name does not compile, so no
// caller writes to a sealed table by forgetting the seal.

#include <crucible/SchemaTable.h>

int main() {
    crucible::SchemaTable table;
    table.register_name(table.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>()), crucible::SchemaHash{0x42},
                        ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(static_cast<const char*>("aten::mm")));
    return 0;
}
