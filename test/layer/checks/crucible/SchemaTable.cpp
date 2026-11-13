// The compile-time checks of crucible/SchemaTable.h.

#include <crucible/SchemaTable.h>

namespace crucible {

// An entry owns the storage of its name, so the table moves it and never
// copies it.
static_assert(std::is_nothrow_move_constructible_v<SchemaEntry> && !std::is_copy_constructible_v<SchemaEntry>);

static_assert(::fixy::no_scoped_view_field_check<SchemaTable>());

}  // namespace crucible
