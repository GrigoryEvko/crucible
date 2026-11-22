// Negative compile test.  This file must fail to compile.
//
// A raw pointer names an object that the walk cannot see the owner of.
// The row walk of the payloads reads the target of a pointer, so it gives
// the payload the empty row.  The transfer gate of the session mint refuses
// the pointer, because the receiver can read the object on another thread
// after the sender ends.  The pointer stands after a receive, so the gate
// reads past the first step of the protocol.
//
// Expected diagnostic: Payload_Not_Owned, with the protocol that holds the
// pointer.

#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace neg_sess_send_raw_pointer_types {
namespace s = ::fixy::session;
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Proto = s::Recv<int, s::Send<int*, s::End>>;
}  // namespace neg_sess_send_raw_pointer_types

int main() {
    namespace eff = ::foundation::effects;
    namespace s = ::fixy::session;
    using neg_sess_send_raw_pointer_types::Proto;
    using neg_sess_send_raw_pointer_types::Wire;
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>> ctx{eff::testing::bg()};
    auto head = s::mint_session<Proto>(ctx, Wire{});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
