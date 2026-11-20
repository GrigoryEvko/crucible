// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A pin holds a handle on the calling thread for the run of a body, and the
// body reaches the handle only through the pinned handle that it gets.  A
// body that captures the handle itself by reference could step it while the
// pin holds it, so the pin refuses the body.
//
// Expected diagnostic: Pin_Body_Holds_A_Handle.

#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace neg_sess_pin_body_captures_the_handle_types {
namespace s = ::fixy::session;
struct Wire {
    int last_sent = 0;
};
using Proto = s::Loop<s::Send<int, s::Continue>>;
}  // namespace neg_sess_pin_body_captures_the_handle_types

int main() {
    namespace s = ::fixy::session;
    using neg_sess_pin_body_captures_the_handle_types::Proto;
    using neg_sess_pin_body_captures_the_handle_types::Wire;
    const auto ctx = ::foundation::effects::testing::foreground();
    auto head = s::mint_session<Proto>(ctx, Wire{});
    auto& outer = head;
    auto back = s::HandleFactory::pin(ctx, std::move(head), [&outer](auto pinned) noexcept {
        static_cast<void>(outer);
        return pinned;
    });
    std::move(back).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
