// Negative-compile test.  This file must fail to compile.
//
// The const doors of a Stopped handle read plain state, such as the count of
// the ops that the detect stage holds.  The thread of the frame can start the
// pipeline at the same time, and the stage then writes that state.  A handle is
// not Shareable, so a body of a second thread captures no reference to const of
// it.

#include <crucible/BackgroundThread.h>

#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>

#include <utility>

namespace {

struct Scope final {};

// The handle and the context are parameters, so the fixture starts no thread.
[[maybe_unused]] void read_beside(crucible::BackgroundThread::Stopped<Scope> const& stopped,
                                  ::fixy::BgLoadCtx const& ctx) {
    auto first = [&stopped]() noexcept { static_cast<void>(stopped.pending_op_count()); };
    auto second = []() noexcept {};
    static_cast<void>(
        ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(first), std::move(second)));
}

}  // namespace

int main() { return 0; }
