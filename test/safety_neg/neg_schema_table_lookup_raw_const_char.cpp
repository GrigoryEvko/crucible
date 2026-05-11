// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for #1007 WRAP-SchemaTab-5
// (SchemaTable::lookup() raw const char* return ->
// Tagged<Borrowed<const char, SchemaTable>, source::Sanitized>).
//
// Premise: lookup() borrows from SchemaTable-owned interned storage.
// A caller must not receive a naked const char* without explicitly
// crossing the typed-borrow boundary via .value().data().  The
// returned value carries both owner lifetime (Borrowed<..., SchemaTable>)
// and post-registration provenance (source::Sanitized).
//
// Distinct mismatch class from neg_schema_table_lookup_cross_tag.cpp:
//   * This fixture: raw const char* assignment rejected because Tagged
//     has no implicit conversion to the payload pointer.
//   * Companion: cross-tag assignment rejected because Sanitized
//     provenance is not External provenance.

#include <crucible/SchemaTable.h>

int main() {
  crucible::SchemaTable table;

  // MUST fail: lookup() returns SchemaTable::LookupName, not raw
  // const char*.
  const char* raw = table.lookup(crucible::SchemaHash{0xA11CE});
  return raw == nullptr ? 0 : 1;
}
