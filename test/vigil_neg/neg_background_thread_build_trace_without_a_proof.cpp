// Negative-compile test.  This file must fail to compile.
//
// build_trace_from writes the state of the trace build of the build stage.
// A caller outside the pipeline takes that state in a frame with the proof
// that no pipeline runs (with_trace_build_state, prove_quiescence).  So a
// build with no proof does not compile, and a build while a stage writes the
// state ends the process at the proof.

#include <crucible/BackgroundThread.h>

#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

namespace {

// The thread is a parameter, so the fixture builds no ring and no log for it.
[[maybe_unused]] void build_with(crucible::BackgroundThread& background) {
    background.with_trace_build_state([](crucible::BackgroundThread::TraceBuildState&) noexcept {});
}

}  // namespace

int main() { return 0; }
