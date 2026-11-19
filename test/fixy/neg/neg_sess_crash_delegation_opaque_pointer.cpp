// A pointer to void crosses a crash-aware session.  Its static type does
// not name the object it points at, and that object can be an endpoint or
// a token.  The payload walk refuses it in every session, so the permission
// flow of the protocol does not close, and the mint refuses the payload
// before the delegation query.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Proto = s::Offer<s::Recv<void*, s::End>, s::Recv<s::Crash<Bob>, s::End>>;

int main() {
    s::CrashCell<Bob, Alice> cell;
    s::CrashCell<Alice, Bob> own;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(ctx, Wire{}, cell, s::mint_crash_writer(own));
    (void)handle;
    return 0;
}
