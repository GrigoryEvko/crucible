// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// BackgroundThread::RegionReadyCall is the CallRef of the noexcept
// signature, and not the CallRef of the same signature without noexcept.
// The second assertion below claims the opposite, so it fails.  If a change
// dropped the noexcept from RegionReadyCall, the assertion would hold.
// CallRef has only the noexcept form, so BackgroundThread.h would then not
// compile either.
//
// The sibling fixture neg_background_thread_callback_throwing_fnptr_rejected
// shows that a function that is not noexcept does not fit RegionReadyCall.

#include <crucible/BackgroundThread.h>

#include <type_traits>

namespace c = crucible;

// The whole signature of RegionReadyCall, so the one difference the
// assertion can see is the noexcept.
using non_noexcept_call =
    ::fixy::CallRef<void(::foundation::effects::Bg const&, c::BackgroundThread::PublishStage const&,
                         c::BackgroundThread::PublishedRegion const&)>;
static_assert(
    std::is_same_v<c::BackgroundThread::RegionReadyCall,
                   ::fixy::CallRef<void(::foundation::effects::Bg const&, c::BackgroundThread::PublishStage const&,
                                        c::BackgroundThread::PublishedRegion const&) noexcept>>,
    "the signature above must track RegionReadyCall, or this fixture fails for the wrong reason");

static_assert(std::is_same_v<c::BackgroundThread::RegionReadyCall, non_noexcept_call>,
              "RegionReadyCall carries noexcept, so it is not the CallRef of the plain signature");

int main() { return 0; }
