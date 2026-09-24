// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A lifetime start over bytes can produce an object of an implicit-lifetime
// type with no constructor call.  The tree admits one route to a lifetime
// start, the checked start, and it requires an implicit-lifetime element.
// The publish-stage proof has a user-provided constructor and no copy, so
// it is not an implicit-lifetime type and the checked start refuses it.
//
// Expected diagnostic: the ImplicitLifetimeThroughout constraint fails.

#include <crucible/BackgroundThread.h>
#include <foundation/Lifetime.h>

int main() {
    using Stage = crucible::BackgroundThread::PublishStage;
    alignas(Stage) unsigned char storage[1]{};
    auto forged = ::foundation::lifetime::start_as_array<Stage>(storage, 1);
    (void)forged;
    return 0;
}
