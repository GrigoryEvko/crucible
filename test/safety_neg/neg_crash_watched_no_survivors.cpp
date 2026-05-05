// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-045: CrashWatchedHandle may only be constructed for a PeerTag
// whose crash-recovery inheritance lattice declares at least one
// survivor.  Otherwise peer death would drop permissions without a typed
// recovery target.

#include <crucible/bridges/CrashTransport.h>

#include <utility>

namespace proto = crucible::safety::proto;
using crucible::safety::OneShotFlag;

struct PeerWithoutSurvivors {};
struct Channel {};

int main() {
    using P = proto::Send<int, proto::End>;

    OneShotFlag flag;
    proto::PermissionedSessionHandle<P, proto::EmptyPermSet, Channel> psh{
        Channel{}};

    proto::CrashWatchedHandle<P, Channel, PeerWithoutSurvivors> bad{
        std::move(psh), flag};
    (void)bad;
    return 0;
}
