// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_consumer_session over an SPSC channel whose payload carries the
// background row refuses the foreground context.  A received payload brings
// its row into the receiver, so the receive side of the protocol does not fit
// the foreground context either.

#include <crucible/concurrent/_PermissionedSpscChannel.h>
#include <crucible/effects/_Computation.h>
#include <crucible/sessions/SpscSession.h>

namespace eff = ::crucible::effects;
namespace ses = ::crucible::safety::proto::spsc_session;

namespace {
struct Tag {};
using BgInt = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using Channel = ::crucible::concurrent::PermissionedSpscChannel<BgInt, 64, Tag>;
}  // namespace

inline void mint_under_foreground(Channel::ConsumerHandle& handle) {
    auto session = ses::mint_consumer_session<Channel>(eff::HotFgCtx{}, handle);
    (void)session;
}

int main() { return 0; }
