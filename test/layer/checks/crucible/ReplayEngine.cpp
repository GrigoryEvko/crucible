// The compile-time checks of crucible/ReplayEngine.h.

#include <crucible/ReplayEngine.h>

namespace crucible {

static_assert(sizeof(ReplayEngine) == 64, "ReplayEngine: 8 × 8B = 64 bytes (one cache line)");

// A view must not outlive the frame that minted it, so storing one in a field
// would let it escape.
static_assert(::fixy::no_scoped_view_field_check<ReplayEngine>());

}  // namespace crucible
