// Negative compile test.  This file must fail to compile.
//
// The body of run_beside reads a SegmentList through a reference to const.
// The continuation on the calling thread can append to the list at the same
// time, so the door refuses the body (foundation/Closure.h, rule 8).  A body
// that holds a reference to a Sealed of the list passes, as
// test_core_sealed.cpp shows.

#include <fixy/Core.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    ::fixy::SegmentList<int> list;
    ::fixy::SegmentList<int> const& read_list = list;
    auto body = [&read_list]() noexcept { return read_list.size(); };
    auto continuation = [&list]() noexcept { list.append(eff::testing::test().alloc, 5); };
    [[maybe_unused]] auto started =
        ::fixy::thread::run_beside(ctx, ::fixy::thread::ThreadOptions{}, std::move(body), continuation);
    return 0;
}
