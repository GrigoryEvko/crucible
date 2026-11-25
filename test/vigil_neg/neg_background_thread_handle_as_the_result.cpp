// Negative-compile test.  This file must fail to compile.
//
// The frame of with_background_thread ends the thread when its body returns.  A
// body that gives its handle would give a handle of a thread that ended.  The
// type of the handle holds the scope brand of the frame, so the door refuses
// the body.

#include <crucible/BackgroundThread.h>

#include <foundation/effects/Effect.h>

namespace {

// The sides are parameters, so the fixture builds no ring and no log.
[[maybe_unused]] void keep_the_handle(crucible::BackgroundThread::TraceRingSide& ring_side,
                                      crucible::BackgroundThread::MetaLogSide& log_side) {
    static_cast<void>(crucible::with_background_thread(
        ::foundation::effects::testing::test().alloc, ring_side, log_side, {},
        [](crucible::BackgroundThread const&, auto stopped) noexcept { return stopped; }));
}

}  // namespace

int main() { return 0; }
