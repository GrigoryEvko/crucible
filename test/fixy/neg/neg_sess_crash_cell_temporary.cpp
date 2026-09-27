// The decorator keeps the address of the detector cell.  A temporary
// cell dies at the end of the statement, so every later step would
// read a dead object.  The overload for a temporary is deleted.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Proto = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Bob>, s::End>>;

int main() {
    s::PeerCrashCell own;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(ctx, Wire{}, s::PeerCrashCell{}, s::mint_crash_writer(own));
    (void)handle;
    return 0;
}
