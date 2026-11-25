// Negative-compile test.  This file must fail to compile.
//
// A body of with_trace_build_state that writes a Ref of the state into an
// object of its caller takes the state out of the frame.  The frame refuses a
// body whose captures reach the state.

#include <crucible/BackgroundThread.h>

#include <fixy/Core.h>

namespace {

using State = crucible::BackgroundThread::TraceBuildState;
struct KeptBrand {};
using KeptRef = ::fixy::Option<::fixy::Ref<State, KeptBrand>>;

// The thread and the slot are parameters, so the fixture builds no ring and
// no log.
[[maybe_unused]] void keep_the_state(crucible::BackgroundThread& background, KeptRef& kept) {
    background.with_trace_build_state(background.prove_quiescence(),
                                      [&kept](State&) noexcept { static_cast<void>(kept.is_some()); });
}

}  // namespace

int main() { return 0; }
