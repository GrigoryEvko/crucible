// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// lookup() borrows from the interned storage that the schema table owns.
// It returns SchemaTable::LookupName, which is
// Tagged<Borrowed<const char, SchemaTable>, source::Interned>.  The value
// carries the lifetime of its owner and the provenance of the table.  A
// caller gets a raw const char* only through .value().data().
//
// This fixture refuses a raw const char* because Tagged has no implicit
// conversion to its payload pointer.  The companion fixture
// neg_schema_table_lookup_cross_tag refuses a different provenance tag.

#include <crucible/SchemaTable.h>

int main() {
    crucible::SchemaTable table;

    const char* raw = table.lookup(crucible::SchemaHash{0xA11CE});
    return raw == nullptr ? 0 : 1;
}
