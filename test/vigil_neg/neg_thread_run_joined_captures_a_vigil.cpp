// Negative-compile test.  This file must fail to compile.
//
// A Vigil belongs to the thread that records into it, so run_joined refuses a
// body that uses a Vigil through a reference from another thread
// (foundation/Closure.h).
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>

int main() {
    crucible::Vigil vigil{};
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto body = [&vigil]() noexcept { vigil.release_producer_claim(); };
    [[maybe_unused]] auto started = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body));
    return 0;
}
