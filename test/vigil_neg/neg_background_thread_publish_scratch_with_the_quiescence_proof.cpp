// Negative-compile test.  This file must fail to compile.
//
// A job of run_on_publish_stage writes the scratch arena of the publish stage,
// also while no pipeline runs.  So the proof that no pipeline runs shows
// nothing about that arena, and a read of its bytes takes the publish-stage
// proof.

#include <crucible/BackgroundThread.h>

namespace {

// The thread is a parameter, so the fixture builds no ring and no log for it.
[[maybe_unused]] size_t read_bytes(crucible::BackgroundThread& background) {
    return background.publish_scratch_retained_bytes(background.prove_quiescence()).raw();
}

}  // namespace

int main() { return 0; }
