// Negative-compile test.  This file must fail to compile.
//
// A job of run_on_publish_stage writes the scratch arena of the publish stage,
// also while no pipeline runs.  So a Stopped handle shows nothing about that
// arena, and a read of its bytes takes the publish-stage proof.

#include <crucible/BackgroundThread.h>

namespace {

struct Scope final {};

// The thread and the handle are parameters, so the fixture builds no ring and
// no log.
[[maybe_unused]] size_t read_bytes(crucible::BackgroundThread const& background,
                                   crucible::BackgroundThread::Stopped<Scope> const& stopped) {
    return background.publish_scratch_retained_bytes(stopped).raw();
}

}  // namespace

int main() { return 0; }
