// The compile-time checks of crucible/BackgroundThread.h.

#include <crucible/BackgroundThread.h>

#include <foundation/Transfer.h>

namespace crucible {

namespace {

// The brand of a handle that no frame gives.
struct ProbeScope final {};

using ProbeStopped = BackgroundThread::Stopped<ProbeScope>;
using ProbeRunning = BackgroundThread::Running<ProbeScope>;

}  // namespace

// The pipeline thread captures this, and a BackgroundThread with no region
// callback holds a Ref of its own member ignore_regions_.  A copy or a move
// would leave the two behind, so a BackgroundThread has no copy and no move.
static_assert(!__is_constructible(BackgroundThread, BackgroundThread const&)
              && !__is_constructible(BackgroundThread, BackgroundThread&&));
static_assert(!__is_assignable(BackgroundThread&, BackgroundThread const&)
              && !__is_assignable(BackgroundThread&, BackgroundThread&&));

// Each BackgroundThread holds the consumer side of a trace ring and of a
// metadata log from its construction, so no constructor builds one with no
// side, and the stages read the two with no test.  Its constructor takes the
// key of the frame door and of the holder, so no code makes a thread with no
// handle, and no code outside them builds a key.
static_assert(__is_constructible(BackgroundThread, BackgroundThreadKey const&, ::foundation::effects::Alloc,
                                 BackgroundThread::TraceRingSide&, BackgroundThread::MetaLogSide&,
                                 BackgroundThread::Setup const&));
static_assert(!__is_constructible(BackgroundThread, BackgroundThreadKey const&, ::foundation::effects::Alloc,
                                  BackgroundThread::Setup const&));
static_assert(!__is_constructible(BackgroundThread, ::foundation::effects::Alloc, BackgroundThread::TraceRingSide&,
                                  BackgroundThread::MetaLogSide&, BackgroundThread::Setup const&));
static_assert(!__is_constructible(BackgroundThreadKey)
              && !__is_constructible(BackgroundThreadKey, BackgroundThreadKey const&)
              && !__is_trivially_copyable(BackgroundThreadKey));

// Each thread has one handle.  A handle moves and does not copy, and an
// assignment would lose one.  It stays on the thread of its frame, and no two
// threads read it.
static_assert(__is_constructible(ProbeStopped, ProbeStopped&&)
              && !__is_constructible(ProbeStopped, ProbeStopped const&));
static_assert(__is_constructible(ProbeRunning, ProbeRunning&&)
              && !__is_constructible(ProbeRunning, ProbeRunning const&));
static_assert(!__is_assignable(ProbeStopped&, ProbeStopped&&) && !__is_assignable(ProbeRunning&, ProbeRunning&&));
static_assert(!__is_constructible(ProbeStopped, BackgroundThread&)
              && !__is_constructible(ProbeRunning, BackgroundThread&));
static_assert(!::foundation::transfer::Sendable<ProbeStopped> && !::foundation::transfer::Shareable<ProbeStopped>);
static_assert(!::foundation::transfer::Sendable<ProbeRunning> && !::foundation::transfer::Shareable<ProbeRunning>);

// The holder keeps the one handle of its thread, so it does not move, and no
// two threads share it.  Each other thread has the thread itself as const.
static_assert(!__is_constructible(StartedBackgroundThread, StartedBackgroundThread&&)
              && !__is_constructible(StartedBackgroundThread, StartedBackgroundThread const&));
static_assert(!::foundation::transfer::Shareable<StartedBackgroundThread>);
static_assert(::foundation::transfer::Shareable<BackgroundThread>);

// The class states synchronized{}.  The check of the statement exempts its
// doors (the exemption of foundation/Transfer.h), so the class is Synchronized.
static_assert(::foundation::transfer::Synchronized<BackgroundThread>);

// The publish stage and the readers of the totals use the cell of the count
// at the same time.  Each bump and each relaxed peek takes the proof of the
// publish stage, which no other thread holds.
static_assert(::foundation::transfer::Synchronized<BackgroundThread::TotalProcessedCell>);

}  // namespace crucible
