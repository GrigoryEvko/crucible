// Negative-compile test.  This file must fail to compile.
//
// set_numerics writes a plain member of the thread, so two threads that call it
// at one time race.  It is a door of the Stopped handle, which stays on the
// thread of its frame, so a body of a second thread cannot capture the handle.

#include <crucible/BackgroundThread.h>

#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>

#include <utility>

namespace {

struct Scope final {};

// The handle and the context are parameters, so the fixture starts no thread.
[[maybe_unused]] void race(crucible::BackgroundThread::Stopped<Scope>& stopped, ::fixy::BgLoadCtx const& ctx) {
    auto first = [&stopped]() noexcept { stopped.set_numerics(crucible::NumericsHash{1}); };
    auto second = []() noexcept {};
    static_cast<void>(
        ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(first), std::move(second)));
}

}  // namespace

int main() { return 0; }
