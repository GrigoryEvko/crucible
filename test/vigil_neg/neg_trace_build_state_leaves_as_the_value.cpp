// Negative-compile test.  This file must fail to compile.
//
// The frame of with_trace_build_state lends the state only until its body
// returns.  A body that gives the state back would give a reference that a
// later start makes shared with the build stage, so the frame refuses a value
// that reaches the state.

#include <crucible/BackgroundThread.h>

#include <fixy/Core.h>

namespace {

struct Scope final {};
using State = crucible::BackgroundThread::TraceBuildState;

// The handle is a parameter, so the fixture builds no ring and no log.
[[maybe_unused]] void take_the_state(crucible::BackgroundThread::Stopped<Scope>& stopped) {
    State& state = stopped.with_trace_build_state([](State& lent) noexcept -> State& { return lent; });
    static_cast<void>(state);
}

}  // namespace

int main() { return 0; }
