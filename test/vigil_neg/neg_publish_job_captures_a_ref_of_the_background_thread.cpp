// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A job of run_on_publish_stage gets the doors of the publish stage through
// its PublishStageView.  A capture of a Ref of the BackgroundThread would give
// the job the state of each other stage, such as the iteration detector of the
// detect stage, so the door refuses each capture that reaches a
// BackgroundThread.

#include <crucible/BackgroundThread.h>

#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

int main() {
    crucible::BackgroundThread background{::foundation::effects::testing::test().alloc};
    const ::fixy::Ref<crucible::BackgroundThread> whole{::fixy::to(background)};
    background.run_on_publish_stage(
        [whole](crucible::BackgroundThread::PublishStageView&,
                crucible::BackgroundThread::PublishStage const&) noexcept { static_cast<void>(whole->works_built()); });
    return 0;
}
