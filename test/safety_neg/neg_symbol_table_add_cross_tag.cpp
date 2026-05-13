// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for #1032 WRAP-SymTab-4
// (SymbolTable::add() raw SymbolId return -> InternalSymbolId).
//
// Premise: a freshly minted SymbolId is source::FromInternal, not
// source::External.  From-disk / FFI IDs must enter through their own
// validation lane; SymbolTable::add() output must not be silently
// reused as if it crossed that external boundary.
//
// Distinct mismatch class from neg_symbol_table_add_raw_symbol_id.cpp:
//   * Companion: internal Tagged value cannot decay to raw SymbolId.
//   * This fixture: internal Tagged value cannot convert to an
//     external Tagged value.

#include <crucible/SymbolTable.h>
#include <crucible/Ops.h>
#include <crucible/safety/Tagged.h>

int main() {
  crucible::SymbolTable table;
  using ExternalSymbolId = crucible::safety::Tagged<
      crucible::SymbolId, crucible::safety::source::External>;

  ExternalSymbolId id =
      table.add(crucible::SymKind::SIZE, crucible::ExprFlags::IS_INTEGER);
  (void)id;
  return 0;
}
