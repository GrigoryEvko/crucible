// Negative compile test.  This file must fail to compile.
//
// A send calls its transport again while it waits for room, and a call is no
// consume.  A transport that holds a linear end by move would end it with no
// consume, so WriteTransport of fixy/session/Transport.h refuses it.
//
// Expected diagnostic: no send of the handle takes the transport, and the
// note names HoldsNoLinearValue of the transport.

#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>

#include <utility>

namespace neg_sess_transport_holds_linear_types {
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
}  // namespace neg_sess_transport_holds_linear_types

int main() {
    namespace s = ::fixy::session;
    using neg_sess_transport_holds_linear_types::End;
    using neg_sess_transport_holds_linear_types::Proto;
    using neg_sess_transport_holds_linear_types::Wire;
    const auto ctx = ::foundation::effects::testing::foreground();
    auto head = s::mint_session<Proto>(ctx, Wire{});
    End end{7};
    auto next = std::move(head).send(1, [held = std::move(end)](Wire& wire, int& value) noexcept {
        wire.last_sent = value + held.handle;
        return true;
    });
    std::move(next).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
