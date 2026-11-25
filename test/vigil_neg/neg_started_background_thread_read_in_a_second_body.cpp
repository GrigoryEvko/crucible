// Negative-compile test.  This file must fail to compile.
//
// The holder of a started thread keeps the one Running handle of it, so no two
// threads share the holder, also through a reference to const.  A second thread
// takes the thread itself as const, which is Shareable.

#include <crucible/BackgroundThread.h>

#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>

#include <utility>

namespace {

// The holder and the context are parameters, so the fixture starts no thread.
[[maybe_unused]] void read_beside(crucible::StartedBackgroundThread const& started, ::fixy::BgLoadCtx const& ctx) {
    auto first = [&started]() noexcept { static_cast<void>(started.shared().works_built()); };
    auto second = []() noexcept {};
    static_cast<void>(
        ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(first), std::move(second)));
}

}  // namespace

int main() { return 0; }
