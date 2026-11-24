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

// Each BackgroundThread holds the consumer side of a trace ring and of a
// metadata log from its construction, so no constructor builds one with no
// side, and the stages read the two with no test.
static_assert(!__is_constructible(BackgroundThread, ::foundation::effects::Alloc)
              && !__is_constructible(BackgroundThread, ::foundation::effects::Alloc, PipelineLimits const&));
static_assert(__is_constructible(BackgroundThread, ::foundation::effects::Alloc, BackgroundThread::TraceRingSide&,
                                 BackgroundThread::MetaLogSide&));

// The class states synchronized{}, and some doors of it take no proof.  The
// check of the statement exempts its doors (the exemption of
// foundation/Transfer.h), so the class is Synchronized.  The keyed doors of
// the owner-key member series replace those doors and the exemption.
static_assert(::foundation::transfer::Synchronized<BackgroundThread>);

// The publish stage and the readers of the totals use the cell of the count
// at the same time.  Each bump and each relaxed peek takes the proof of the
// publish stage, which no other thread holds.
static_assert(::foundation::transfer::Synchronized<BackgroundThread::TotalProcessedCell>);

}  // namespace crucible
