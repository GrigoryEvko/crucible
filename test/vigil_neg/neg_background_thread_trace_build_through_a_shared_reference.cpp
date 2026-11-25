// Negative-compile test.  This file must fail to compile.
//
// build_trace_from writes the state of the trace build of the build stage.
// Only a Stopped handle lends that state, in a frame while no pipeline runs
// (with_trace_build_state).  So a build through a shared reference does not
// compile, and no build runs beside a stage.

#include <crucible/BackgroundThread.h>

namespace {

// The thread is a parameter, so the fixture builds no ring and no log for it.
[[maybe_unused]] void build_with(crucible::BackgroundThread const& background) {
    background.with_trace_build_state([](crucible::BackgroundThread::TraceBuildState&) noexcept {});
}

}  // namespace

int main() { return 0; }
