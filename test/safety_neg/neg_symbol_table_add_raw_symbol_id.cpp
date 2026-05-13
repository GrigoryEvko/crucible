// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for #1032 WRAP-SymTab-4
// (SymbolTable::add() raw SymbolId return -> InternalSymbolId).
//
// Premise: SymbolIds freshly minted by SymbolTable::add() carry
// source::FromInternal provenance.  Assigning the return value directly
// to a raw SymbolId must fail; callers must explicitly peel the tag
// with .value() at the point where a raw index is actually required.
//
// Distinct mismatch class from neg_symbol_table_add_cross_tag.cpp:
//   * This fixture: raw SymbolId assignment rejected because Tagged
//     has no implicit conversion to the payload type.
//   * Companion: cross-tag assignment rejected because internal
//     provenance is not external provenance.

#include <crucible/SymbolTable.h>
#include <crucible/Ops.h>

int main() {
  crucible::SymbolTable table;
  crucible::SymbolId id =
      table.add(crucible::SymKind::SIZE, crucible::ExprFlags::IS_INTEGER);
  (void)id;
  return 0;
}
