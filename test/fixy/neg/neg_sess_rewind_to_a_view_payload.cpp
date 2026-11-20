// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A rewind opens a new session on the Resource of a handle at End, at a
// protocol of its own, and it takes no context.  So no row gate reads the
// payloads of that protocol.  The gate of the rewind asks the transfer of
// each payload, as each mint asks it: a View refers to the elements of its
// sender, and a transport can give it to a thread that runs after the
// sender ends.
//
// Expected diagnostic: Payload_Not_Owned, with the protocol of the rewind
// that holds the View.

#include <fixy/Core.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace neg_sess_rewind_to_a_view_payload_types {
namespace s = ::fixy::session;
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using SendsAView = s::Send<::fixy::View<int const>, s::End>;
}  // namespace neg_sess_rewind_to_a_view_payload_types

int main() {
    namespace eff = ::foundation::effects;
    namespace s = ::fixy::session;
    using neg_sess_rewind_to_a_view_payload_types::SendsAView;
    using neg_sess_rewind_to_a_view_payload_types::Wire;
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>> ctx{eff::testing::bg()};
    auto at_end = s::mint_session<s::End>(ctx, Wire{});
    auto rewound = s::HandleFactory::rewind<SendsAView, void>(std::move(at_end));
    std::move(rewound).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
