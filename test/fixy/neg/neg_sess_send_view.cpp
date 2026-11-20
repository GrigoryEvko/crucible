// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A View refers to the elements of its sender, and a frame of the sender
// can hold them.  A transport can give the payload to a thread that runs
// after the sender ends, so the transfer gate of the session mint refuses
// the protocol.  The gate stands before the row walk of the payloads, so
// the refusal comes from the transfer gate, whatever the rosters of that
// walk name.
//
// Expected diagnostic: Payload_Not_Owned, with the protocol that holds the
// View.

#include <fixy/Core.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace neg_sess_send_view_types {
namespace s = ::fixy::session;
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Proto = s::Send<::fixy::View<int const>, s::End>;
}  // namespace neg_sess_send_view_types

int main() {
    namespace eff = ::foundation::effects;
    namespace s = ::fixy::session;
    using neg_sess_send_view_types::Proto;
    using neg_sess_send_view_types::Wire;
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>> ctx{eff::testing::bg()};
    auto head = s::mint_session<Proto>(ctx, Wire{});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
