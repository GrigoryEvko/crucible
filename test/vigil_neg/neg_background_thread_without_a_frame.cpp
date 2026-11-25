// Negative-compile test.  This file must fail to compile.
//
// A BackgroundThread stands in a frame or in a holder, which give the one
// handle of it.  Its constructor takes the key that only the frame door and the
// holder have, so no other code makes a thread with no handle.

#include <crucible/BackgroundThread.h>

#include <foundation/effects/Effect.h>

namespace {

// The sides are parameters, so the fixture builds no ring and no log.
[[maybe_unused]] void make_a_thread(crucible::BackgroundThread::TraceRingSide& ring_side,
                                    crucible::BackgroundThread::MetaLogSide& log_side) {
    const crucible::BackgroundThread background{::foundation::effects::testing::test().alloc, ring_side, log_side,
                                                crucible::BackgroundThread::Setup{}};
    static_cast<void>(background.works_built());
}

}  // namespace

int main() { return 0; }
