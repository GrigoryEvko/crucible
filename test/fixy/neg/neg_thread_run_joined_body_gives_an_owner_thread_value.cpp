// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The value of a body of the thread door crosses from the thread of the body
// to the caller, so it is Sendable.  A value of a class of one owner thread
// is not Sendable (foundation/Transfer.h), so the door refuses a body that
// gives one.
//
// Expected diagnostic: no candidate of run_joined, with a note on the shape
// of the body.

#include <fixy/os/ThreadTasks.h>
#include <foundation/Transfer.h>
#include <foundation/effects/Ctx.h>

namespace {

struct[[= ::foundation::transfer::thread_bound{}]] OwnerThreadValue {
    int value = 0;
};

}  // namespace

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    auto body = []() noexcept { return OwnerThreadValue{}; };
    [[maybe_unused]] auto started = ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, body);
    return 0;
}
