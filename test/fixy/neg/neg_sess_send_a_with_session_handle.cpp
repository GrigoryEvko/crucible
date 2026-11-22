// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// with_session runs its body on the calling thread and closes the session on
// that thread, so it opens the session under check::Pinned.  The pinned
// policy is thread_bound (foundation/Transfer.h), so a handle of the body is
// not Sendable.  A body of run_joined runs on another thread, so the door
// refuses a body that owns a handle of with_session.
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace neg_sess_send_a_with_session_handle_types {
namespace s = ::fixy::session;
struct Wire {
    int last_sent = 0;
};
using Proto = s::Send<int, s::End>;
constexpr auto write_value = [](Wire& wire, int& value) noexcept {
    wire.last_sent = value;
    return true;
};
}  // namespace neg_sess_send_a_with_session_handle_types

int main() {
    namespace s = ::fixy::session;
    using neg_sess_send_a_with_session_handle_types::Proto;
    using neg_sess_send_a_with_session_handle_types::Wire;
    using neg_sess_send_a_with_session_handle_types::write_value;
    const auto ctx = ::foundation::effects::testing::foreground();
    [[maybe_unused]] const Wire back =
        s::with_session<Proto>(ctx, Wire{}, [](auto head) noexcept -> decltype(std::move(head).send(1, write_value)) {
            ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
            auto body = [moved = std::move(head)]() mutable noexcept {
                static_cast<void>(std::move(moved).send(1, write_value).close());
            };
            [[maybe_unused]] auto started =
                ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body));
            ::fixy::unreachable();
        });
    return 0;
}
