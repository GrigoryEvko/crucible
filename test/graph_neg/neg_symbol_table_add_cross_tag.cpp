// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for the return type of SymbolTable::add()
// (InternalSymbolId, not a raw SymbolId).
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
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

int main() {
    crucible::SymbolTable table;
    using ExternalSymbolId = ::fixy::Tagged<crucible::SymbolId, ::fixy::tags::source::External>;

    ExternalSymbolId id = table.add(crucible::SymKind::SIZE, crucible::ExprFlags::IS_INTEGER);
    (void)id;
    return 0;
}
