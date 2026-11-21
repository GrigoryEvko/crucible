// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A bound handle stays on the thread of its bind: HandleFactory::bind gives
// the handle the pinned policy, which is thread_bound (foundation/Transfer.h),
// so the handle is not Sendable.  A body of run_joined runs on another thread,
// so the door refuses a body that owns a bound handle.
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace neg_sess_send_a_bound_handle_types {
namespace s = ::fixy::session;
struct Wire {
    int last_sent = 0;
};
using Proto = s::Loop<s::Send<int, s::Continue>>;
}  // namespace neg_sess_send_a_bound_handle_types

int main() {
    namespace s = ::fixy::session;
    using neg_sess_send_a_bound_handle_types::Proto;
    using neg_sess_send_a_bound_handle_types::Wire;
    const auto ctx = ::foundation::effects::testing::foreground();
    auto bound = s::HandleFactory::bind(ctx, s::mint_session<Proto>(ctx, Wire{}));
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto body = [moved = std::move(bound)]() mutable noexcept {
        std::move(moved).detach(s::detach_reason::TestInstrumentation{});
    };
    [[maybe_unused]] auto started = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, body);
    return 0;
}
