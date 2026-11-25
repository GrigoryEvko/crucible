// Negative-compile test.  This file must fail to compile.
//
// A job that posts a job waits on its own stage, and that wait has no end.  Only
// a handle posts a job, and the door refuses a job with a capture that reaches a
// BackgroundThread, such as a handle.

#include <crucible/BackgroundThread.h>

namespace {

struct Scope final {};
using View = crucible::BackgroundThread::PublishStageView;
using Stage = crucible::BackgroundThread::PublishStage;

// The handle is a parameter, so the fixture builds no ring and no log.
[[maybe_unused]] void post_in_a_job(crucible::BackgroundThread::Running<Scope>& running) {
    running.run_on_publish_stage([&running](View&, Stage const&) noexcept {
        running.run_on_publish_stage([](View&, Stage const&) noexcept {});
    });
}

}  // namespace

int main() { return 0; }
