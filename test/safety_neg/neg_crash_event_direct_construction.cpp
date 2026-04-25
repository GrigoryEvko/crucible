// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: direct construction of CrashEvent<PeerTag, Resource> via
// the public-aggregate-init pattern that pre-#430 code used —
// `std::unexpected(CrashEvent<P, R>{recovered})`.
//
// Per #430, CrashEvent's constructor is pass-key-protected: only
// `wrap_crash_return` can mint the WrapCrashReturnKey, so direct
// construction is rejected with a "WrapCrashReturnKey is private"
// diagnostic from the pass-key class's private default ctor.  This
// makes the bug from #400 (wrapper crash-paths returning unexpected
// without first detaching the inner SessionHandle) STRUCTURALLY
// IMPOSSIBLE — the only path to construct a CrashEvent goes through
// the helper that bundles the detach.

#include <crucible/bridges/CrashTransport.h>

#include <expected>
#include <utility>

using namespace crucible::safety::proto;

struct ServerPeer {};
struct Channel { int session_id = 0; };

int main() {
    // Pre-#430 pattern that #400's debugging caught — wrapper code
    // attempting to fabricate a CrashEvent without detaching its
    // inner.  Now a compile error: WrapCrashReturnKey's default ctor
    // is private and only friends wrap_crash_return.
    auto bad = std::unexpected(
        CrashEvent<ServerPeer, Channel>{Channel{42}});
    (void)bad;
    return 0;
}
