// A bare reception from an unreliable peer has no crash branch, so the
// survivor would wait forever for a peer that is gone (rule 3, LMCS
// 2025 Def. 4.3).  The crash-aware mint refuses it.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Proto = s::Send<int, s::Recv<int, s::End>>;

int main() {
    s::PeerCrashCell cell;
    s::PeerCrashCell own;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(ctx, Wire{}, cell, s::mint_crash_writer(own));
    (void)handle;
    return 0;
}
