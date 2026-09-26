// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// register_name takes a SchemaTable::MutableView.  A SealedView does not
// convert to a MutableView, and no overload takes a SealedView.  When the
// table is sealed, no call reaches register_name.

#include <crucible/SchemaTable.h>

int main() {
    crucible::SchemaTable t;
    t.seal();
    auto sv = t.mint_sealed_view();

    t.register_name(sv, crucible::SchemaHash{0x42},
                    ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(static_cast<const char*>("aten::mm")));
    return 0;
}
