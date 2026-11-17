// The body of a scheduling frame gives the borrow of the proof back.  The
// proof ends with the frame, so the borrow would name an object that no
// longer exists.  The result of a frame holds no borrow and names no brand
// of the frame, so the call binds to the deleted overload.

#include <fixy/Ctx.h>
#include <fixy/os/Sched.h>

namespace {

[[maybe_unused]] void attempt(::fixy::BgDrainCtx const& ctx) {
    [[maybe_unused]] auto refused = fixy::sched::mint_scheduler_policy<fixy::SchedulerPolicy_v::Other>(
        ctx, [](auto proof) noexcept { return proof; });
}

}  // namespace

int main() { return 0; }
