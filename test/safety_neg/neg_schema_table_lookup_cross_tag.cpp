// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for #1007 WRAP-SchemaTab-5
// (SchemaTable::lookup() raw const char* return ->
// Tagged<Borrowed<const char, SchemaTable>, source::Sanitized>).
//
// Premise: lookup() returns a Sanitized SchemaTable borrow.  Treating
// that post-validation borrow as source::External would erase the
// trust boundary and let already-sanitized table output flow back into
// raw-FFI-only lanes.
//
// Distinct mismatch class from neg_schema_table_lookup_raw_const_char.cpp:
//   * Companion: raw const char* assignment rejected at the pointer
//     extraction boundary.
//   * This fixture: cross-tag assignment rejected because
//     Tagged<T, Sanitized> is not Tagged<T, External>.

#include <crucible/ir001/SchemaTable.h>
#include <crucible/safety/Tagged.h>

int main() {
  crucible::SchemaTable table;

  using ExternalLookupName = crucible::safety::Tagged<
      crucible::SchemaTable::BorrowedName,
      crucible::safety::source::External>;

  // MUST fail: lookup() returns source::Sanitized, and no implicit
  // Sanitized -> External retag exists.
  ExternalLookupName wrong =
      table.lookup(crucible::SchemaHash{0xA11CE});
  return wrong.value().data() == nullptr ? 0 : 1;
}
