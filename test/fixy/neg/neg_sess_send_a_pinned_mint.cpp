// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A session that opens under check::Pinned keeps each of its handles on the
// thread of the mint: the pinned policy is thread_bound (foundation/Transfer.h),
// so the handle is not Sendable.  A body of run_joined runs on another thread,
// so the door refuses a body that owns such a handle.
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace neg_sess_send_a_pinned_mint_types {
namespace s = ::fixy::session;
struct Wire {
    int last_sent = 0;
};
using Proto = s::Loop<s::Send<int, s::Continue>>;
}  // namespace neg_sess_send_a_pinned_mint_types

int main() {
    namespace s = ::fixy::session;
    using neg_sess_send_a_pinned_mint_types::Proto;
    using neg_sess_send_a_pinned_mint_types::Wire;
    const auto ctx = ::foundation::effects::testing::foreground();
    auto head = s::mint_session<Proto, s::check::Pinned>(ctx, Wire{});
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto body = [moved = std::move(head)]() mutable noexcept {
        std::move(moved).detach(s::detach_reason::TestInstrumentation{});
    };
    [[maybe_unused]] auto started = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, body);
    return 0;
}
