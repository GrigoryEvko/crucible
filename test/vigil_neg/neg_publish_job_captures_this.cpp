// Negative-compile test.  This file must fail to compile.
//
// run_on_publish_stage gives a job the view of the publish stage and its proof
// as parameters, and it checks the closure of the job with the closure model.
// A job runs on the thread of the publish stage, so a capture of `this` of its
// owner, a plain address of an object that the caller owns, is refused.

#include <crucible/BackgroundThread.h>

#include <foundation/effects/Effect.h>

namespace {

struct Owner {
    crucible::BackgroundThread background{::foundation::effects::testing::test().alloc};
    unsigned count = 0;

    void count_regions() {
        background.run_on_publish_stage([this](crucible::BackgroundThread::PublishStageView& owner,
                                               crucible::BackgroundThread::PublishStage const& stage) noexcept {
            count += owner.region_count(stage);
        });
    }
};

}  // namespace

int main() {
    Owner owner{};
    owner.count_regions();
    return static_cast<int>(owner.count);
}
