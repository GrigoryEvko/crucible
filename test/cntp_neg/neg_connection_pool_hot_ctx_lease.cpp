// A lease changes pool state and waits on the blocking gate of the pool.  The
// foreground context owns neither Bg nor Test nor Block, so it cannot lease.

#include <crucible/cntp/ConnectionPoolRuntime.h>
#include <fixy/Ctx.h>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace fe = ::foundation::effects;

int main() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::HotFgCtx hot{fe::testing::foreground()};
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::Tcp, 1, 1>(init);

    cog::CogIdentity remote{};
    remote.uuid = cog::Uuid{1, 2};
    auto lease = pool.lease(hot, remote);
    (void)lease;
    return 0;
}
