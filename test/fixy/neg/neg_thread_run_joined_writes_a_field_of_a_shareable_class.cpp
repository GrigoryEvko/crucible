// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class that states shareable{} states that two threads can read an object
// of it through a reference to const.  It does not state that two threads can
// write it.  Two bodies of run_joined that write a plain field of such a class
// through a reference that is not const race, so the door refuses them
// (foundation/Closure.h, rule 7).  Rust: unsafe impl Sync gives only &T, and a
// write of a plain field through &T does not compile.
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/os/ThreadTasks.h>
#include <foundation/Transfer.h>
#include <foundation/effects/Ctx.h>

struct[[= ::foundation::transfer::shareable{}]] ReadMostly {
    int count = 0;
};

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    ReadMostly shared{};
    auto first = [&shared]() noexcept { ++shared.count; };
    auto second = [&shared]() noexcept { ++shared.count; };
    [[maybe_unused]] auto started =
        ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(first), std::move(second));
    return shared.count;
}
