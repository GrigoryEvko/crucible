// Negative compile test.  This file must fail to compile.
//
// The body of run_beside holds a prefix of a list.  A prefix refers to elements
// of type int as a View does, and the type does not tell that no thread writes
// them, so the door refuses the body (foundation/Closure.h, rule 8).  A body
// that borrows the LiveList and takes the prefix itself passes, as
// test_core_live_list.cpp shows.

#include <fixy/Core.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/core/LiveList.h>
#include <foundation/effects/Ctx.h>

#include <utility>

class LogWriter;
struct AppendRole final {};

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    ::foundation::core::LiveList<int, ::foundation::SoleWriterProof<LogWriter, AppendRole>> list;
    auto body = [held = list.prefix()]() noexcept { return held.size(); };
    [[maybe_unused]] auto started = ::fixy::thread::run_beside(ctx, ::fixy::thread::ThreadOptions{}, std::move(body),
                                                               [&list]() noexcept { return list.prefix().size(); });
    return 0;
}
