// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-045: CrashWatchedHandle may only be constructed for a PeerTag
// whose crash-recovery inheritance lattice declares at least one
// survivor.  Otherwise peer death would drop permissions without a typed
// recovery target.

#include <crucible/bridges/CrashTransport.h>
#include <crucible/sessions/SessionMint.h>

#include <utility>

namespace proto = crucible::safety::proto;
namespace eff = crucible::effects;
using crucible::safety::OneShotFlag;

struct PeerWithoutSurvivors {};
struct Channel {};

int main() {
    using P = proto::Send<int, proto::End>;

    eff::HotFgCtx ctx{};
    OneShotFlag flag;
    auto psh = proto::mint_permissioned_session<P>(ctx, Channel{});

    proto::CrashWatchedHandle<P, Channel, PeerWithoutSurvivors> bad{
        std::move(psh), flag};
    (void)bad;
    return 0;
}
