// Negative-compile test.  This file must fail to compile.
//
// run_on_publish_stage gives a job the view of the publish stage and its proof
// as parameters, and it checks the closure of the job with the closure model.
// The job uses its captures on the thread of the publish stage, so the door
// refuses a capture of `this` when the type of the owner is not Sendable.  The
// owner of this fixture holds a handle of its thread, so it is not Sendable.
// The door admits the same capture of an owner with only a count.

#include <crucible/BackgroundThread.h>

namespace {

struct Scope final {};

// The handle is a parameter of the constructor, so the fixture builds no ring
// and no log.
struct Owner {
    crucible::BackgroundThread::Running<Scope>& running;
    unsigned count = 0;

    void count_regions() {
        running.run_on_publish_stage([this](crucible::BackgroundThread::PublishStageView& owner,
                                            crucible::BackgroundThread::PublishStage const& stage) noexcept {
            count += owner.region_count(stage);
        });
    }
};

[[maybe_unused]] void count_with(crucible::BackgroundThread::Running<Scope>& running) {
    Owner owner{.running = running};
    owner.count_regions();
}

}  // namespace

int main() { return 0; }
