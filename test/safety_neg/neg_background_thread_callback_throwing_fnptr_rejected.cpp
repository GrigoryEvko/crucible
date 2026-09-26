// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// BackgroundThread::RegionReadyCallback::Fn is a noexcept function pointer.
// The callback runs on the publish stage between the transaction commit and
// the watchdog observation, so an exception out of it would leave that work
// half done.  A function that is not noexcept does not convert to Fn, and
// this aggregate initialization is refused.  With noexcept on the function
// below, the file compiles.
//
// The sibling fixture neg_background_thread_callback_type_identity_drift
// pins the type of Fn itself.

#include <crucible/BackgroundThread.h>

namespace c = crucible;

namespace {

// The parameter list of Fn, without the noexcept.
void throwing_callback(void* /*ctx*/, ::foundation::effects::Bg const& /*bg*/,
                       c::BackgroundThread::PublishStage /*stage*/, c::RegionNode* /*region*/) {}

}  // namespace

int main() {
    c::BackgroundThread::RegionReadyCallback cb{
        .ctx = nullptr,
        .fn = &throwing_callback,
    };
    (void)cb;
    return 0;
}
