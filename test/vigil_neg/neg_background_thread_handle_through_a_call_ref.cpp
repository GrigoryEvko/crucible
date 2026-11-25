// Negative-compile test.  This file must fail to compile.
//
// A region callback runs on the publish stage, and it exists before the frame
// of its thread.  A body that gave its handle to the callback through a CallRef
// would let the callback post a job on its own stage, and that wait has no end.
// The type of the handle holds the scope brand of the frame, so a CallRef
// refuses a closure that captures the handle.

#include <crucible/BackgroundThread.h>

#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

namespace {

using View = crucible::BackgroundThread::PublishStageView;
using Stage = crucible::BackgroundThread::PublishStage;

// The sides are parameters, so the fixture builds no ring and no log.
[[maybe_unused]] void give_the_handle(crucible::BackgroundThread::TraceRingSide& ring_side,
                                      crucible::BackgroundThread::MetaLogSide& log_side) {
    crucible::with_background_thread(::foundation::effects::testing::test().alloc, ring_side, log_side, {},
                                     [](crucible::BackgroundThread const&, auto stopped) noexcept {
                                         auto post = [&stopped]() noexcept {
                                             stopped.run_on_publish_stage([](View&, Stage const&) noexcept {});
                                         };
                                         const ::fixy::CallRef<void() noexcept> call{post};
                                         static_cast<void>(sizeof(call));
                                     });
}

}  // namespace

int main() { return 0; }
