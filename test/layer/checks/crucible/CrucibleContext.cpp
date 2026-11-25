// The compile-time checks of crucible/CrucibleContext.h.

#include <crucible/CrucibleContext.h>

namespace crucible {

static_assert(sizeof(DispatchResult) == 8, "DispatchResult: 1+1+2+4 = 8 bytes");

static_assert(sizeof(CrucibleContext) == 208,
              "CrucibleContext: 72 engine + 24 mode, iterations and region + 24 external reads + 80 pool + 8 "
              "divergences = 208");

// A view must not outlive the frame that minted it, so storing one in a field
// would let it escape.
static_assert(::fixy::no_scoped_view_field_check<CrucibleContext>());

}  // namespace crucible
