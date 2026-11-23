// Negative-compile test. This file must fail to compile.
//
// A registration enters the write section of the table.  A writer whose
// context owns Div spins while another writer holds the section, and the
// writer of a Vigil's producer claim, the one writer of the process, takes the
// section with one compare-exchange.  So the registration takes the context
// of the writer, and a registration with no context does not compile.
//
// Expected diagnostic: SchemaTable::register_name has no candidate that takes
// no context.

#include <crucible/SchemaTable.h>
#include <crucible/Vigil.h>

int main() {
    crucible::SchemaTable table;
    const auto view = table.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>())
                          .expect("an open table mints a mutable view");
    (void)table.register_name(::foundation::effects::testing::test().alloc, view, crucible::SchemaHash{0x42},
                              crucible::InternalSchemaName{::fixy::TextView{"aten::mm"}});
    return 0;
}
