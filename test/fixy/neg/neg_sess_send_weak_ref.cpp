// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A WeakRef refers to an object of its sender, and the object can end
// before the receiver reads it.  A transport can give the payload to a
// thread that runs after the sender ends, so the transfer gate of the
// session mint refuses the protocol.  The gate stands before the row walk
// of the payloads, so the refusal comes from the transfer gate, whatever
// the rosters of that walk name.
//
// Expected diagnostic: Payload_Not_Owned, with the protocol that holds the
// WeakRef.

#include <fixy/Borrowed.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace neg_sess_send_weak_ref_types {
namespace s = ::fixy::session;
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Proto = s::Send<::fixy::WeakRef<int const>, s::End>;
}  // namespace neg_sess_send_weak_ref_types

int main() {
    namespace eff = ::foundation::effects;
    namespace s = ::fixy::session;
    using neg_sess_send_weak_ref_types::Proto;
    using neg_sess_send_weak_ref_types::Wire;
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>> ctx{eff::testing::bg()};
    auto head = s::mint_session<Proto>(ctx, Wire{});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
