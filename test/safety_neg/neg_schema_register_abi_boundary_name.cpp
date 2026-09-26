// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A name that comes through the C ABI is tagged ABIBoundary.  The C ABI
// entry checks the length and the terminator, and then retags the name to
// Sanitized.  A call that skips the retag hands register_schema_name a name
// that no check read, and SchemaNameSource refuses it.
//
// The companion fixture neg_schema_register_external_name refuses a name
// from a trace file at the register_name member.
//
// Expected diagnostic: the SchemaNameSource constraint of
// register_schema_name is not satisfied for source::ABIBoundary.

#include <crucible/SchemaTable.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

int main() {
    crucible::SchemaTable table;
    const auto view = table.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>());
    const auto name = ::fixy::mint_tagged<::fixy::tags::source::ABIBoundary>(static_cast<const char*>("aten::mm"));
    return crucible::register_schema_name(*view, crucible::SchemaHash{0x42}, name) ? 0 : 1;
}
