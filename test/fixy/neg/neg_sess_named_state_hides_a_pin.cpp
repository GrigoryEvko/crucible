// Negative compile test.  This file must fail to compile.
//
// The name of a named state is a class with a member type `protocol`.  A
// VendorPinned also has such a member, the protocol that it pins.  A named
// state over a pin would stand for that protocol without the pin, so it
// would hide the vendor from each walk.  The registry reads no node for a
// name that is a combinator, and the mint refuses the protocol.
//
// Expected diagnostic: Protocol_Unregistered_Combinator, which names the
// state.
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

namespace neg_sess_named_state_hides_a_pin_types {
struct Wire {};
}  // namespace neg_sess_named_state_hides_a_pin_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_named_state_hides_a_pin_types;
    using Pinned = s::VendorPinned<s::VendorBackend::NV, s::Send<int, s::End>>;
    const ::fixy::BgDrainCtx ctx{::foundation::effects::testing::bg()};
    auto refused = s::mint_session<s::Named<Pinned>>(ctx, Wire{});
    std::move(refused).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
