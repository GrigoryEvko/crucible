// Negative-compile test.  This file must fail to compile.
//
// The drain-stage proof opens the consumer side of the trace ring of a Vigil
// or of a rig that a background thread binds.  Only the background thread
// builds one.  A foreground function that built its own would drain the ring
// beside the drain stage, or reset it while the drain stage reads its
// counters.
//
// Expected diagnostic: the constructor is private.

#include <crucible/BackgroundThread.h>

int main() {
    const crucible::BackgroundThread::DrainStage stage;
    (void)stage;
    return 0;
}
