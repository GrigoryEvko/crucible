// Negative-compile test.  This file must fail to compile.
//
// The Vigil is the one owner that holds a started thread.  The constructor of
// the holder is private, so each other owner of a thread opens its frame.

#include <crucible/BackgroundThread.h>

#include <fixy/Ctx.h>

namespace {

// The context and the sides are parameters, so the fixture starts no thread.
[[maybe_unused]] void hold_a_thread(::fixy::InitLoadCtx const& startup,
                                    crucible::BackgroundThread::TraceRingSide& ring_side,
                                    crucible::BackgroundThread::MetaLogSide& log_side) {
    const crucible::StartedBackgroundThread started{startup,
                                                    startup.cap().alloc,
                                                    ring_side,
                                                    log_side,
                                                    crucible::BackgroundThread::Setup{},
                                                    crucible::NumericsHash{},
                                                    crucible::BackgroundThread::PlanTarget{}};
    static_cast<void>(started.shared().works_built());
}

}  // namespace

int main() { return 0; }
