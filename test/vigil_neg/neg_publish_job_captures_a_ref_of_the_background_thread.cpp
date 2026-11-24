// Negative-compile test.  This file must fail to compile.
//
// A job of run_on_publish_stage gets the doors of the publish stage through
// its PublishStageView.  A capture of a Ref of the BackgroundThread would give
// the job the state of each other stage, such as the iteration detector of the
// detect stage, so the door refuses each capture that reaches a
// BackgroundThread.

#include <crucible/BackgroundThread.h>

#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

namespace {

// The thread is a parameter, so the fixture builds no ring and no log for it.
[[maybe_unused]] void post_with(crucible::BackgroundThread& background) {
    const ::fixy::Ref<crucible::BackgroundThread> whole{::fixy::erase_brand(::fixy::to(background))};
    background.run_on_publish_stage(
        [whole](crucible::BackgroundThread::PublishStageView&,
                crucible::BackgroundThread::PublishStage const&) noexcept { static_cast<void>(whole->works_built()); });
}

}  // namespace

int main() { return 0; }
