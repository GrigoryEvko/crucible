// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_producer_session over an SPSC channel whose payload carries the
// background row refuses the foreground context.  The foreground context
// admits no background row, so the send side of the protocol does not fit it.
// The refusal is a constraint failure on the mint, not an error inside it.

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

inline void mint_under_foreground(Channel::ProducerHandle& handle) {
    auto session = ses::mint_producer_session<Channel>(eff::HotFgCtx{}, handle);
    (void)session;
}

int main() { return 0; }
