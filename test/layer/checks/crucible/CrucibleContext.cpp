// The compile-time checks of crucible/CrucibleContext.h.

#include <crucible/CrucibleContext.h>

namespace crucible {

static_assert(sizeof(DispatchResult) == 8, "DispatchResult: 1+1+2+4 = 8 bytes");

static_assert(sizeof(CrucibleContext) == 120, "CrucibleContext: 64 engine + 24 cold + 32 pool = 120");

// A view must not outlive the frame that minted it, so storing one in a field
// would let it escape.
static_assert(::fixy::no_scoped_view_field_check<CrucibleContext>());

}  // namespace crucible
