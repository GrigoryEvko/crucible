// Negative-compile test.  This file must fail to compile.
//
// A handle stays on the thread of its frame, which joins the pipeline at its
// end.  A body of a second thread that took the handle by move could start a
// pipeline beside a door of the frame.  A handle is bound to its thread, so it
// is not Sendable.

#include <crucible/BackgroundThread.h>

#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>

#include <utility>

namespace {

struct Scope final {};

// The handle and the context are parameters, so the fixture starts no thread.
[[maybe_unused]] void move_away(crucible::BackgroundThread::Stopped<Scope> stopped, ::fixy::BgLoadCtx const& ctx) {
    auto first = [held = std::move(stopped)]() mutable noexcept { held.rearm_stop(); };
    auto second = []() noexcept {};
    static_cast<void>(
        ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(first), std::move(second)));
}

}  // namespace

int main() { return 0; }
