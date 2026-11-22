// Negative-compile test.  This file must fail to compile.
//
// A producer claim belongs to the thread that holds it, so run_joined refuses
// a body that uses an object of the claim through a reference from another
// thread (foundation/Closure.h).
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Ctx.h>

namespace {

struct ClaimBrand {
    ::foundation::effects::host::ProducerClaim<ClaimBrand> claim{};
};

}  // namespace

int main() {
    ClaimBrand first{};
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto body = [&first]() noexcept { first.claim.release_claim(); };
    [[maybe_unused]] auto started = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body));
    return 0;
}
