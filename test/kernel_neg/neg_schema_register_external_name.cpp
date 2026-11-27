// Negative-compile test. This file must fail to compile.
//
// The schema table admits a name only when its type satisfies
// SchemaNameSource: a Sanitized name (sanitize_schema_name checked it) or an
// internal name (it crossed no boundary).  A name read from a trace file is
// External until the loader checks it.  If register_name admitted it before
// the check, bytes that no check read could become an interned name that
// every reader trusts.
//
// The companion fixture neg_schema_register_abi_boundary_name refuses a
// name from the C ABI at register_schema_name.
//
// Expected diagnostic: no overload of register_name takes a Tagged of
// source::External.

#include <crucible/SchemaTable.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

// A write section of the table.  The fixture never links, so the function
// has no body.
crucible::WriteSection<crucible::SchemaTable>& held_section() noexcept;

int main() {
    crucible::SchemaTable table;
    const auto view = table.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>())
                          .expect("an open table mints a mutable view");
    const auto name = ::fixy::mint_tagged<::fixy::tags::source::External>(::fixy::TextView{"aten::mm"});
    const ::foundation::effects::Alloc allocation = ::foundation::effects::testing::test().alloc;
    return table.register_name(held_section(), allocation, view, name) ? 0 : 1;
}
