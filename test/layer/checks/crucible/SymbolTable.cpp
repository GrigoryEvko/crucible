// The compile-time checks of crucible/SymbolTable.h.

#include <crucible/SymbolTable.h>

namespace crucible {

static_assert(sizeof(SymbolEntry) == 32, "SymbolEntry should be 32 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(SymbolEntry);

static_assert(sizeof(InternalSymbolId) == sizeof(SymbolId));

}  // namespace crucible
