// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// lookup() returns a borrow tagged source::Interned: the schema table
// owns and interns the name.  A borrow tagged source::External is raw input
// from outside the process.  If table output could pass as External, it
// could go into a lane that only a sanitizer may read, and the tag would
// no longer tell the two sources apart.
//
// This fixture refuses a different provenance tag, because
// Tagged<T, Interned> is not Tagged<T, External> and no implicit retag
// exists.  The companion fixture neg_schema_table_lookup_raw_const_char
// refuses a raw const char*.

#include <crucible/SchemaTable.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

int main() {
    crucible::SchemaTable table;

    using ExternalLookupName = ::fixy::Tagged<crucible::SchemaTable::BorrowedName, ::fixy::tags::source::External>;

    ExternalLookupName wrong = table.lookup(crucible::SchemaHash{0xA11CE});
    return wrong.value().data() == nullptr ? 0 : 1;
}
