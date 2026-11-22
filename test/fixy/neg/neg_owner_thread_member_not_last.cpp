// Negative compile test.  This file must fail to compile.
//
// The thread of an owner ends first only when it is the last data member
// of the owner.  A member after it ends before the thread, while the body
// can still read it, so mint_owner_thread refuses the owner.
//
// Expected diagnostic: no candidate of mint_owner_thread, with the note of
// the check of the last member.

#include <fixy/os/OwnedThread.h>
#include <foundation/effects/Ctx.h>

namespace {

namespace eff = ::foundation::effects;
using StarterCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

class Owner {
public:
    Owner() noexcept = default;
    Owner(Owner const&) = delete("the thread borrows the owner at one address");
    Owner& operator=(Owner const&) = delete("the thread borrows the owner at one address");
    Owner(Owner&&) = delete("the thread borrows the owner at one address");
    Owner& operator=(Owner&&) = delete("the thread borrows the owner at one address");
    ~Owner() = default;

    void start() noexcept {
        [[maybe_unused]] auto started = ::fixy::thread::mint_owner_thread<&Owner::thread_, &Owner::request_stop_>(
            StarterCtx{eff::testing::bg()}, ::fixy::thread::ThreadOptions{}, *this,
            [](Owner& owner) noexcept { owner.run_(); });
    }

private:
    void run_() noexcept {}
    void request_stop_() noexcept {}

    ::fixy::thread::OwnerThread<Owner> thread_{};
    int after_ = 0;
};

}  // namespace

int main() {
    Owner owner{};
    owner.start();
    return 0;
}
