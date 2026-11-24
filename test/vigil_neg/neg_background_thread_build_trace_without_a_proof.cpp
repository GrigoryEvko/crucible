// Negative-compile test.  This file must fail to compile.
//
// build_trace_from writes the state of the trace build of the build stage.
// A caller outside the pipeline takes that state with the proof that no
// pipeline runs (trace_build_state, prove_quiescence), so a build with no
// proof does not compile, and a build while a stage writes the state ends
// the process at the proof.

#include <crucible/BackgroundThread.h>

#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

namespace {

// The thread is a parameter, so the fixture builds no ring and no log for it.
[[maybe_unused]] void build_with(crucible::BackgroundThread& background) {
    auto& state = background.trace_build_state();
    static_cast<void>(state);
}

}  // namespace

int main() { return 0; }
