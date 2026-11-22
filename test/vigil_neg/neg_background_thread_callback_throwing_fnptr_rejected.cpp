// Negative-compile test.  This file must fail to compile.
//
// BackgroundThread::RegionReadyCall is a CallRef of a noexcept signature.
// The callback runs on the publish stage between the transaction commit and
// the watchdog observation, so an exception out of it would leave that work
// half done.  A function that is not noexcept does not fit the CallRef, and
// of<> refuses it.  With noexcept on the function below, the file compiles.
//
// The sibling fixture neg_background_thread_callback_type_identity_drift
// pins the type of RegionReadyCall itself.

#include <crucible/BackgroundThread.h>

namespace c = crucible;

namespace {

// The parameter list of RegionReadyCall, without the noexcept.
void throwing_callback(::foundation::effects::Bg const& /*bg*/, c::BackgroundThread::PublishStage const& /*stage*/,
                       c::BackgroundThread::PublishedRegion const& /*published*/) {}

}  // namespace

int main() {
    const c::BackgroundThread::RegionReadyCall call = c::BackgroundThread::RegionReadyCall::of<&throwing_callback>();
    (void)call;
    return 0;
}
