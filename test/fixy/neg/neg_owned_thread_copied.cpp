// Negative compile test.  This file must fail to compile.
//
// One thread has one owner of its join.  A copy of an OwnedThread would join
// the thread two times, so the copy constructor is deleted.
//
// Expected diagnostic: use of the deleted copy constructor of OwnedThread,
// with its reason.

#include <fixy/os/OwnedThread.h>
#include <foundation/effects/Ctx.h>

#include <utility>

int main() {
    namespace eff = ::foundation::effects;
    using StarterCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
    auto started = ::fixy::thread::mint_owned_thread(StarterCtx{eff::testing::bg()}, ::fixy::thread::ThreadOptions{},
                                                     []() noexcept {});
    ::fixy::thread::OwnedThread thread = std::move(started).expect("the thread starts");
    ::fixy::thread::OwnedThread second = thread;
    std::move(second).join();
    return 0;
}
