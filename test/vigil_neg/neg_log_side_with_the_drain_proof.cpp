// Negative-compile test.  This file must fail to compile.
//
// The consumer side of the metadata log opens only with the proof of the
// publish stage.  The drain stage holds the proof of the consumer side of the
// trace ring, and that proof does not open the log: the drain stage would
// release records that the build stage has not read.  The function takes a
// proof of the drain stage as the drain stage holds it, so the fixture builds
// no proof.

#include <crucible/BackgroundThread.h>

void release_with_the_ring_proof(crucible::BackgroundThread::MetaLogSide& side,
                                 crucible::BackgroundThread::DrainStage const& stage) noexcept;

void release_with_the_ring_proof(crucible::BackgroundThread::MetaLogSide& side,
                                 crucible::BackgroundThread::DrainStage const& stage) noexcept {
    side.with_end(stage, [](crucible::MetaLog::Consumer& log) noexcept {
        crucible::MetaLog::advance_tail(log, crucible::MetaLog::produced(log));
    });
}

int main() { return 0; }
