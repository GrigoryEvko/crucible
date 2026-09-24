// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The publish-stage proof admits a write to the state that the publish
// stage owns, such as the transaction log of a Vigil.  Only the background
// thread builds one.  A foreground function that built its own would write
// that state beside the publish stage.
//
// Expected diagnostic: the constructor is private.

#include <crucible/BackgroundThread.h>

int main() {
    const crucible::BackgroundThread::PublishStage stage;
    (void)stage;
    return 0;
}
