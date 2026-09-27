// A reception with no crash branch is found inside a loop body, after a
// guarded reception on the first iteration.  The walk reaches every
// position, so the crash-aware mint refuses the protocol.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Proto = s::Loop<s::Offer<s::Recv<int, s::Send<int, s::Recv<int, s::Continue>>>, s::Recv<s::Crash<Bob>, s::End>>>;

int main() {
    s::PeerCrashCell cell;
    s::PeerCrashCell own;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(ctx, Wire{}, cell, s::mint_crash_writer(own));
    (void)handle;
    return 0;
}
