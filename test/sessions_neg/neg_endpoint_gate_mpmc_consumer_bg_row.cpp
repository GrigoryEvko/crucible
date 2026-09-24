// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_mpmc_consumer_session over an MPMC channel whose payload carries the
// background row refuses the foreground context.  The receive side of the
// protocol brings the row into the receiver, which the context does not admit.

#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/effects/_Computation.h>
#include <crucible/sessions/MpmcChannelSession.h>

namespace eff = ::crucible::effects;
namespace ses = ::crucible::safety::proto::mpmc_channel_session;

namespace {
struct Tag {};
using BgInt = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using Channel = ::crucible::concurrent::PermissionedMpmcChannel<BgInt, 64, Tag>;
}  // namespace

inline void mint_under_foreground(Channel::ConsumerHandle& handle) {
    auto session = ses::mint_mpmc_consumer_session<Channel>(eff::HotFgCtx{}, handle);
    (void)session;
}

int main() { return 0; }
