// Negative compile test.  This file must fail to compile.
//
// A pin calls the body of its caller and does not own it, and a call is no
// consume.  A body that holds a linear end by move would end it with no
// consume, so the pin refuses the body with the reason of the gate.
//
// Expected diagnostic: the deleted pin, and the text of
// CRUCIBLE_LINEAR_BODY_REFUSAL of foundation/Linear.h.

#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>

#include <utility>

namespace neg_sess_pin_body_holds_linear_types {
namespace s = ::fixy::session;
struct Wire {
    int last_sent = 0;
};
using Proto = s::Loop<s::Send<int, s::Continue>>;

struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] End {
    int handle = 0;
    constexpr explicit End(int value) noexcept : handle{value} {}
    constexpr End(End&&) noexcept = default;
    constexpr End& operator=(End&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    End(End const&) = delete("a linear end of the fixture does not copy");
    End& operator=(End const&) = delete("a linear end of the fixture does not copy");
};
}  // namespace neg_sess_pin_body_holds_linear_types

int main() {
    namespace s = ::fixy::session;
    using neg_sess_pin_body_holds_linear_types::End;
    using neg_sess_pin_body_holds_linear_types::Proto;
    using neg_sess_pin_body_holds_linear_types::Wire;
    const auto ctx = ::foundation::effects::testing::foreground();
    auto head = s::mint_session<Proto>(ctx, Wire{});
    End end{7};
    auto back = s::HandleFactory::pin(ctx, std::move(head), [held = std::move(end)](auto pinned) noexcept {
        static_cast<void>(held.handle);
        return pinned;
    });
    std::move(back).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
