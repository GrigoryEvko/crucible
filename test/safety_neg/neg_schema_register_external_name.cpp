// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The schema table admits a name only when its tag satisfies
// SchemaNameSource: Sanitized (a boundary checked it, and the check
// retagged it) or FromInternal (it crossed no boundary).  A name read from
// a trace file is External until the loader checks it.  If register_name
// admitted it before the retag, bytes that no check read could become an
// interned name that every reader trusts.
//
// The companion fixture neg_schema_register_abi_boundary_name refuses a
// name from the C ABI at register_schema_name.
//
// Expected diagnostic: the SchemaNameSource constraint of register_name is
// not satisfied for source::External.

#include <crucible/SchemaTable.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

int main() {
    crucible::SchemaTable table;
    const auto view = table.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>());
    const auto name = ::fixy::mint_tagged<::fixy::tags::source::External>(static_cast<const char*>("aten::mm"));
    return table.register_name(*view, crucible::SchemaHash{0x42}, name) ? 0 : 1;
}
