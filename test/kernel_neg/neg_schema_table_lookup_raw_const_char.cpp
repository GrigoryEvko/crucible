// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// lookup() gives a C text in the interned storage that the schema table
// owns.  It returns an Option of SchemaTable::LookupName, which is
// Tagged<CText, source::Interned>.  The value carries the provenance of the
// table, and only foreign_c_str in an ABI region gives its address.
//
// This fixture refuses a raw const char*, because neither the Option nor
// Tagged has an implicit conversion to a pointer.  The companion fixture
// neg_schema_table_lookup_cross_tag refuses a different provenance tag.

#include <crucible/SchemaTable.h>

int main() {
    crucible::SchemaTable table;

    const char* raw = table.lookup(crucible::SchemaHash{0xA11CE});
    return raw == nullptr ? 0 : 1;
}
