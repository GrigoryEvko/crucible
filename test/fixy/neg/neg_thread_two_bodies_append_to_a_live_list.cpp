// Negative compile test.  This file must fail to compile.
//
// The owner of a LiveList gives one proof of its one writer to two bodies that
// run at the same time.  Each body could append, so the list would have two
// writers.  A proof is not Shareable, so the door of the threads refuses a body
// that borrows one (foundation/Closure.h, rule 6).  A body that borrows only
// the list passes, as test_core_live_list.cpp shows.

#include <fixy/Core.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/core/LiveList.h>
#include <foundation/effects/Ctx.h>

#include <utility>

namespace eff = ::foundation::effects;

struct AppendRole final {};

class LogWriter;
using Proof = ::foundation::SoleWriterProof<LogWriter, AppendRole>;
using Numbers = ::foundation::core::LiveList<int, Proof>;

class LogWriter final {
public:
    static void append_from_two_threads(Numbers& list) {
        eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
        Proof const proof{};
        auto first = [&list, &proof]() noexcept { list.append(proof, eff::testing::test().alloc, 1); };
        auto second = [&list, &proof]() noexcept { list.append(proof, eff::testing::test().alloc, 2); };
        [[maybe_unused]] auto started =
            ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(first), std::move(second));
    }
};

int main() {
    Numbers list;
    LogWriter::append_from_two_threads(list);
    return 0;
}
