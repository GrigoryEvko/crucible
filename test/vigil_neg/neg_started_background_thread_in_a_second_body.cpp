// Negative-compile test.  This file must fail to compile.
//
// A StartedBackgroundThread holds the one Running handle of its thread, and
// run_on_publish_stage of the holder waits on that handle.  Two threads that
// post through one holder would share the handle, so the holder is not
// Shareable, and no body of a second thread captures a reference to it.

#include <crucible/BackgroundThread.h>

#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>

#include <utility>

namespace {

using View = crucible::BackgroundThread::PublishStageView;
using Stage = crucible::BackgroundThread::PublishStage;

// The holder and the context are parameters, so the fixture starts no thread.
[[maybe_unused]] void post_from_two_threads(crucible::StartedBackgroundThread& started, ::fixy::BgLoadCtx const& ctx) {
    auto first = [&started]() noexcept { started.run_on_publish_stage([](View&, Stage const&) noexcept {}); };
    auto second = []() noexcept {};
    static_cast<void>(
        ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(first), std::move(second)));
}

}  // namespace

int main() { return 0; }
