// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The key of the doors of a class states thread_bound{} and not_shareable{}:
// it belongs to the thread that made it.  A body of run_joined runs on another
// thread, so the door refuses a body that borrows the key of the owner, and a
// second thread cannot call a door of the class with that key (THE PROOF OF A
// DOOR of foundation/Transfer.h).
//
// Expected diagnostic: the verdict of the door names the capture of the key.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>

#include <foundation/effects/Ctx.h>

namespace {

struct[[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]] OwnerKey final {
    int uses = 0;
};

}  // namespace

int main() {
    OwnerKey key{};
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto body = [&key]() noexcept { ++key.uses; };
    [[maybe_unused]] auto joined = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body));
    return 0;
}
