// The compile-time checks of crucible/BackgroundThread.h.

#include <crucible/BackgroundThread.h>

namespace crucible {

// The pipeline thread captures this, and a BackgroundThread with no region
// callback holds a Ref of its own member ignore_regions_.  A copy or a move
// would leave the two behind, so a BackgroundThread has no copy and no move.
static_assert(!__is_constructible(BackgroundThread, BackgroundThread const&)
              && !__is_constructible(BackgroundThread, BackgroundThread&&));
static_assert(!__is_assignable(BackgroundThread&, BackgroundThread const&)
              && !__is_assignable(BackgroundThread&, BackgroundThread&&));

// The class states synchronized{}, and nine doors of it take no proof.  The
// check of the statement exempts its doors (THE EXEMPTION of
// foundation/Transfer.h), so the class is Synchronized.  The keyed doors of
// the owner-key member series replace the nine doors and the exemption.
static_assert(::foundation::transfer::Synchronized<BackgroundThread>);

}  // namespace crucible
