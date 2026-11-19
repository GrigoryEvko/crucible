// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The thread holds a pointer to its owner, so the owner stays at one address.
// mint_owner_thread refuses an owner that can move.
//
// Expected diagnostic: no candidate of mint_owner_thread, with the note of
// StaysInPlace.

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
    Owner(Owner&& /*other*/) noexcept {}
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
};

}  // namespace

int main() {
    Owner owner{};
    owner.start();
    return 0;
}
