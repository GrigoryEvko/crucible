// Negative-compile test.  This file must fail to compile.
//
// A start inside a frame of with_trace_build_state would run the build stage
// on the state that the frame lends.  The start ends the process, because the
// frame holds the run state.  The frame also refuses a body whose captures
// reach a BackgroundThread, so the start does not compile.

#include <crucible/BackgroundThread.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>

namespace {

using State = crucible::BackgroundThread::TraceBuildState;

// The thread is a parameter, so the fixture builds no ring and no log for it.
[[maybe_unused]] void start_in_the_frame(crucible::BackgroundThread& background, ::fixy::InitLoadCtx const& startup) {
    background.with_trace_build_state(background.prove_quiescence(),
                                      [&background, &startup](State&) noexcept { background.start(startup); });
}

}  // namespace

int main() { return 0; }
