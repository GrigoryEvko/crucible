// Negative compile test.  This file must fail to compile.
//
// mint_owner_thread asks that two threads can write the owner, because its
// thread writes the owner while the thread that starts it runs on (the
// shared write of foundation/Transfer.h).  An owner that holds a reference
// to a plain int of the caller gives the body a write of that int, and the
// caller writes it too.  A member that is a reference makes the class not
// Synchronized, so the door refuses the owner.
//
// Expected diagnostic: the static assertion of the door names the owner and
// the first part that refuses.

#include <fixy/os/OwnedThread.h>
#include <foundation/effects/Ctx.h>

namespace {

namespace eff = ::foundation::effects;
using StarterCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

class CountsInto {
public:
    explicit CountsInto(int& count) noexcept : count_{count} {}
    CountsInto(CountsInto const&) = delete("the thread borrows the owner at one address");
    CountsInto(CountsInto&&) = delete("the thread borrows the owner at one address");
    CountsInto& operator=(CountsInto const&) = delete("the thread borrows the owner at one address");
    CountsInto& operator=(CountsInto&&) = delete("the thread borrows the owner at one address");
    ~CountsInto() = default;

    void start() noexcept {
        [[maybe_unused]] auto started =
            ::fixy::thread::mint_owner_thread<&CountsInto::thread_, &CountsInto::request_stop_>(
                StarterCtx{eff::testing::bg()}, ::fixy::thread::ThreadOptions{}, *this,
                [](CountsInto& owner) noexcept { ++owner.count_; });
    }

private:
    void request_stop_() noexcept {}

    int& count_;
    ::fixy::thread::OwnerThread<CountsInto> thread_{};
};

}  // namespace

int main() {
    int count = 0;
    CountsInto owner{count};
    owner.start();
    ++count;
    return count;
}
