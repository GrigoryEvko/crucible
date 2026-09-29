// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// BackgroundThread::RegionReadyCallback::Fn is the noexcept function pointer
// type, and not the same type without noexcept.  The second assertion below
// claims the opposite, so it fails.  If a change drops the noexcept from Fn,
// the assertion holds and this file compiles.
//
// The sibling fixture neg_background_thread_callback_throwing_fnptr_rejected
// shows that a function that is not noexcept does not convert to Fn.

#include <crucible/BackgroundThread.h>

#include <type_traits>

namespace c = crucible;

// The whole parameter list of Fn, so the one difference the assertion can
// see is the noexcept.
using non_noexcept_fn = void (*)(void*, ::foundation::effects::Bg const&, c::BackgroundThread::PublishStage,
                                 c::RegionNode*);
static_assert(std::is_same_v<c::BackgroundThread::RegionReadyCallback::Fn,
                             void (*)(void*, ::foundation::effects::Bg const&, c::BackgroundThread::PublishStage,
                                      c::RegionNode*) noexcept>,
              "the parameter list above must track Fn, or this fixture fails for the wrong reason");

static_assert(std::is_same_v<c::BackgroundThread::RegionReadyCallback::Fn, non_noexcept_fn>,
              "RegionReadyCallback::Fn carries noexcept, so it is not the plain function pointer type");

int main() { return 0; }
