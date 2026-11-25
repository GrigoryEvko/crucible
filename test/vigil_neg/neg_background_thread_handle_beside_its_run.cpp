// Negative-compile test.  This file must fail to compile.
//
// The continuation of run_in_row_beside runs beside the pipeline of its handle.
// A door of the handle there would read the state of a stage that runs, so the
// door refuses a continuation that captures a handle.  The continuation gets
// the shared thread.

#include <crucible/BackgroundThread.h>

#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>

namespace {

struct Scope final {};

// The handle and the context are parameters, so the fixture starts no thread.
[[maybe_unused]] void read_beside(crucible::BackgroundThread::Stopped<Scope>& stopped, ::fixy::BgLoadCtx const& ctx) {
    static_cast<void>(stopped.run_in_row_beside(
        ctx, ::fixy::thread::ThreadOptions{},
        [&stopped](crucible::BackgroundThread const&) noexcept { static_cast<void>(stopped.pending_op_count()); }));
}

}  // namespace

int main() { return 0; }
