// A crash branch for a reliable sender is untypable: projection adds
// none, and rule Sub-& forbids a subtype to add one (rule 5).  The
// crash-aware mint refuses it, because the declared reliable set and
// the protocol disagree.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Proto = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Bob>, s::End>>;

int main() {
    s::PeerCrashCell cell;
    s::PeerCrashCell own;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_crash_session<Proto, Alice, Bob, s::ReliableSet<Bob>>(ctx, Wire{}, cell, s::mint_crash_writer(own));
    (void)handle;
    return 0;
}
