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
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(Wire{}, cell);
    (void)handle;
    return 0;
}
