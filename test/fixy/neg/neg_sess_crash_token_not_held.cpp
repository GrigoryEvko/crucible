// A crash-aware session that sends a token it never received.  Its mint
// starts from the empty set, so the send has no token to give, and the
// admission refuses the protocol before the body of the mint runs.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
struct Region {
    using permission_row = foundation::effects::Row<>;
};
}  // namespace

using Proto = s::Select<s::Send<s::Transferable<int, Region>, s::End>>;

int main() {
    s::PeerCrashCell cell;
    s::PeerCrashCell own;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(ctx, Wire{}, cell, s::mint_crash_writer(own));
    (void)handle;
    return 0;
}
