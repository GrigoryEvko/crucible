// The compile-time checks of crucible/SchemaTable.h.

#include <crucible/SchemaTable.h>

namespace crucible {

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(SchemaEntry);

static_assert(::fixy::no_scoped_view_field_check<SchemaTable>());

}  // namespace crucible
