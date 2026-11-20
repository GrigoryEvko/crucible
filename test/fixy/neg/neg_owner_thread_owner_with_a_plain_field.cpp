// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_owner_thread hands its body a reference that is not const to the
// owner, and the thread that starts it keeps the owner and runs on.  So two
// threads write the owner at the same time, and the door asks that two
// threads can write it (THE SHARED WRITE of foundation/Transfer.h).  This
// owner has a plain count that the body and the starter both write.  Rust: a
// thread that borrows a value beside its starter needs Arc<T> with T: Sync.
//
// Expected diagnostic: the static assertion of the door names the owner and
// the first part that refuses.

#include <fixy/os/OwnedThread.h>
#include <foundation/effects/Ctx.h>

namespace {

namespace eff = ::foundation::effects;
using StarterCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

struct Counted {
    Counted() = default;
    Counted(Counted const&) = delete("the thread borrows the owner at one address");
    Counted(Counted&&) = delete("the thread borrows the owner at one address");
    Counted& operator=(Counted const&) = delete("the thread borrows the owner at one address");
    Counted& operator=(Counted&&) = delete("the thread borrows the owner at one address");
    ~Counted() = default;

    void request_stop() noexcept {}

    int count = 0;
    ::fixy::thread::OwnerThread<Counted> thread_{};
};

}  // namespace

int main() {
    Counted owner{};
    auto body = [](Counted& self) noexcept {
        for (int turn = 0; turn < 1000; ++turn)
            ++self.count;
    };
    auto started = ::fixy::thread::mint_owner_thread<&Counted::thread_, &Counted::request_stop>(
        StarterCtx{eff::testing::bg()}, ::fixy::thread::ThreadOptions{}, owner, body);
    for (int turn = 0; turn < 1000; ++turn)
        ++owner.count;
    return started.is_ok() ? owner.count : 0;
}
