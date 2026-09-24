// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_mpsc_consumer_session over an MPSC channel whose payload carries the
// background row refuses the foreground context.  The receive side of the
// protocol brings the row into the receiver, which the context does not admit.

#include <crucible/effects/_Computation.h>
#include <crucible/fixy/Substr.h>

namespace eff = ::crucible::effects;
namespace mpsc = ::crucible::fixy::substr::mpsc;

namespace {
struct Tag {};
using BgInt = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using Channel = mpsc::PermissionedMpscChannel<BgInt, 64, Tag>;
}  // namespace

inline void mint_under_foreground(Channel::ConsumerHandle& handle) {
    auto session = mpsc::mint_mpsc_consumer_session<Channel>(eff::HotFgCtx{}, handle);
    (void)session;
}

int main() { return 0; }
