// The compile-time checks of crucible/ReplayEngine.h.

#include <crucible/ReplayEngine.h>

namespace crucible {

// The guard values, the cursor, the matched op and the slot table take the
// first 56 bytes, and the View of the ops takes the last 16.
static_assert(sizeof(ReplayEngine) == 72, "ReplayEngine: 16 guard + 16 cursor + 8 matched + 16 table + 16 ops = 72");

// A view must not outlive the frame that minted it, so storing one in a field
// would let it escape.
static_assert(::fixy::no_scoped_view_field_check<ReplayEngine>());

}  // namespace crucible
