// Negative-compile test.  This file must fail to compile.
//
// A start inside a frame of with_trace_build_state would run the build stage
// on the state that the frame lends.  Only the handle starts a pipeline, and
// the frame refuses a body whose captures reach a BackgroundThread, such as the
// handle.

#include <crucible/BackgroundThread.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>

#include <utility>

namespace {

struct Scope final {};
using State = crucible::BackgroundThread::TraceBuildState;

// The handle and the context are parameters, so the fixture builds no ring and
// no log.
[[maybe_unused]] void start_in_the_frame(crucible::BackgroundThread::Stopped<Scope>& stopped,
                                         ::fixy::InitLoadCtx const& startup) {
    stopped.with_trace_build_state(
        [&stopped, &startup](State&) noexcept { static_cast<void>(std::move(stopped).start(startup)); });
}

}  // namespace

int main() { return 0; }
